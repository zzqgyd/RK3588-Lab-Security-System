/**
 * ============================================================
V4L2 摄像头采集完整流程
============================================================

start():
    open()                 → 打开摄像头设备 /dev/video21
    VIDIOC_S_FMT           → 设置格式：YUYV、分辨率、帧率
    VIDIOC_REQBUFS         → 请求内核分配缓冲区（MMAP模式）
    VIDIOC_QUERYBUF x N    → 查询每个缓冲区的信息
    mmap() x N             → 把内核缓冲区映射到用户空间
    VIDIOC_QBUF x N        → 把所有空缓冲区交给内核（等待填充数据）
    VIDIOC_STREAMON        → 开启采集

capture():
    VIDIOC_QBUF            → 归还上一帧（如果有）
    VIDIOC_DQBUF           → 获取一帧数据（阻塞等待）
    返回数据指针/大小/宽高

stop():
    VIDIOC_STREAMOFF       → 停止采集
    munmap() x N           → 解除每个缓冲区的映射
    close()                → 关闭设备

============================================================
环形缓冲区机制

   QBUF → 空buf还给内核 → 内核填充数据 → DQBUF取出 → 应用使用
    ↑                                                       │
    └───────────────────────────────────────────────────────┘
 */
// ================================================================
// v4l2_camera.cpp — V4L2 摄像头采集实现
// 功能：
//   1. 打开摄像头 /dev/video21
//   2. 设置 YUYV 格式、分辨率、帧率
//   3. 申请4个环形缓冲区（MMAP模式）
//   4. 采集时返回 mmap 虚拟地址
//   5. ★ 支持导出 DMA-BUF fd（零拷贝给DRM显示）
// ================================================================

#include "v4l2_camera.h"
#include <iostream>
#include <cstring>
#include <fcntl.h>              // open(), O_RDWR
#include <unistd.h>             // close()
#include <sys/ioctl.h>          // ioctl()
#include <sys/mman.h>           // mmap(), munmap()

// ================================================================
// 构造函数：初始化所有成员变量为默认值
// ================================================================
V4L2Camera::V4L2Camera()
    : fd_(-1)                   // 设备文件描述符，-1 表示未打开
    , width_(640)               // 默认宽度
    , height_(480)              // 默认高度
    , stride_width_(0)          // 行跨度（启动时由驱动返回）
    , buffers_(nullptr)         // 内核缓冲区信息数组（v4l2_buffer）
    , mmap_addrs_(nullptr)      // mmap 映射后的用户空间地址数组
    , num_buffers_(0)           // 缓冲区数量
    , running_(false)           // 是否正在采集
    , current_buf_index_(0)     // 当前持有的缓冲区索引
    , buffer_owned_(false) {}   // 是否持有一个未归还的缓冲区

// ================================================================
// 析构函数：确保停止采集并释放所有资源
// ================================================================
V4L2Camera::~V4L2Camera() { stop(); }

// ================================================================
// start：打开摄像头并开始采集
// 参数：
//   device：设备路径，如 "/dev/video21"
//   width/height：期望的图像尺寸
//   fps：期望帧率
// 返回：0 成功，-1 失败
// ================================================================
int V4L2Camera::start(const std::string& device, int width, int height, int fps) {
    // ========== 1. 打开设备 ==========
    fd_ = open(device.c_str(), O_RDWR);                  // 读写方式打开
    if (fd_ < 0) {
        std::cerr << "[V4L2] Cannot open " << device << std::endl;
        return -1;
    }

    width_  = width;
    height_ = height;

    // ========== 2. 设置视频格式 ==========
    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;   // 视频捕获
    fmt.fmt.pix.width       = width_;                        // 图像宽度
    fmt.fmt.pix.height      = height_;                       // 图像高度
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;             // YUYV 格式：每像素2字节
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;               // 逐行扫描（非交错）

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {                // 应用格式设置
        std::cerr << "[V4L2] Set format failed" << std::endl;
        close(fd_);
        return -1;
    }

    // ========== 3. 获取实际 stride（行跨度字节数）==========
    stride_width_ = fmt.fmt.pix.bytesperline;                // 驱动返回的真实行跨度
    if (stride_width_ == 0)
        stride_width_ = width_ * 2;                          // 保底值：YUYV 每像素2字节

    printf("[V4L2] Stride: %d bytes, Image: %dx%d\n",
           stride_width_, width_, height_);

    // ========== 4. 设置帧率 ==========
    struct v4l2_streamparm parm = {0};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;          // 分子：1
    parm.parm.capture.timeperframe.denominator = fps;        // 分母：fps → 1/fps 秒/帧
    ioctl(fd_, VIDIOC_S_PARM, &parm);                        // 应用帧率设置

    // ========== 5. 申请环形缓冲区（MMAP 模式）==========
    struct v4l2_requestbuffers req = {0};
    req.count  = 4;                                          // 申请4个缓冲区（环形使用）
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;                           // MMAP模式：驱动分配，应用映射
    //内核分配物理连续内存（DMA-BUF）
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[V4L2] Request buffers failed" << std::endl;
        close(fd_);
        return -1;
    }

    num_buffers_ = req.count;                                // 实际分配的缓冲区数量
    buffers_     = new v4l2_buffer[num_buffers_];            // 存储每个缓冲区信息
    mmap_addrs_  = new void*[num_buffers_];                  // 存储每个 mmap 映射地址

    // ========== 6. 逐个映射缓冲区并放入内核队列 ==========
    for (int i = 0; i < num_buffers_; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;                                      // 要查询哪个缓冲区

        ioctl(fd_, VIDIOC_QUERYBUF, &buf);                   // 查询缓冲区信息（长度、偏移）
        buffers_[i] = buf;                                   // 保存缓冲区信息

        mmap_addrs_[i] = mmap(                               // 映射到用户空间
            NULL,                                            // 让内核选择虚拟地址
            buf.length,                                      // 映射大小
            PROT_READ | PROT_WRITE,                          // 可读可写
            MAP_SHARED,                                      // 共享：驱动写入，应用可见
            fd_,                                             // 设备文件
            buf.m.offset                                     // 缓冲区在设备文件中的偏移
        );

        ioctl(fd_, VIDIOC_QBUF, &buf);                       // 空缓冲区交给内核（等待填充图像）
    }

    // ========== 7. 开启视频流采集 ==========
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_, VIDIOC_STREAMON, &type);                      // 启动采集

    running_ = true;
    std::cout << "[V4L2] Camera started: " << width_ << "x" << height_
              << " YUYV " << fps << "fps" << std::endl;
    return 0;
}

// ================================================================
// capture：获取一帧图像数据（阻塞等待）
// 参数：
//   data     [OUT] 图像数据指针（指向 mmap 内存，不释放）
//   size     [OUT] 数据实际大小（字节）
//   width    [OUT] 图像宽度
//   height   [OUT] 图像高度
//   stride_w [OUT] 行跨度（字节）
//   stride_h [OUT] 列跨度（等于 height）
// 返回：0 成功，-1 失败
// ================================================================
int V4L2Camera::capture(uint8_t** data, size_t* size,
                        int* width, int* height,
                        int* stride_w, int* stride_h) {
    if (!running_) return -1;

    // ========== 2. 等待并获取新一帧 ==========
    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0)                  // 出队：阻塞等新帧
        return -1;

    // ========== 3. 返回帧数据 ==========
    *data     = (uint8_t*)mmap_addrs_[buf.index];            // mmap 映射的虚拟地址
    *size     = buf.bytesused;                               // 实际有效数据大小
    *width    = width_;                                      // 图像宽度
    *height   = height_;                                     // 图像高度
    *stride_w = stride_width_;                               // 行跨度（字节）
    *stride_h = height_;                                     // 列跨度（YUYV无纵向填充）

    // ========== 4. 记录当前持有状态 ==========
    current_buf_index_ = buf.index;                          // 记下索引（归还和导出时用）
    buffer_owned_ = true;                                    // 标记持有一帧未归还
    return 0;
}

// ================================================================
// ★ export_dma_fd：导出当前帧的 DMA-BUF fd
// 调用时机：capture() 成功之后，下一帧 capture() 之前
// 返回：DMA-BUF fd（可直接给 DRM/RGA），失败返回 -1
// 
// 原理：
//   V4L2 的 MMAP 缓冲区本质是内核分配的 DMA-BUF
//   VIDIOC_EXPBUF 让内核导出一个 fd 指向这块内存
//   该 fd 可以跨进程传给 DRM/RGA，实现零拷贝
// ================================================================
int V4L2Camera::export_dma_fd() {
    if (!buffer_owned_) {                                    // 先有 capture() 才能导出
        fprintf(stderr, "[V4L2] No buffer owned, call capture() first\n");
        return -1;
    }

    struct v4l2_exportbuffer expbuf = {0};
    expbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;              // 视频捕获类型
    expbuf.index = current_buf_index_;                       // 当前持有的缓冲区索引
    expbuf.flags = O_CLOEXEC;                                // exec 时自动关闭

    if (ioctl(fd_, VIDIOC_EXPBUF, &expbuf) < 0) {
        perror("[V4L2] VIDIOC_EXPBUF");
        return -1;
    }

    return expbuf.fd;                                        // 返回 DMA-BUF 文件描述符
}

// 显式归还指定缓冲区（外部帧管理用）
int V4L2Camera::qbuf(int index) {
    if (fd_ < 0 || index < 0 || index >= num_buffers_) return -1;

    struct v4l2_buffer return_buf = {0};
    return_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return_buf.memory = V4L2_MEMORY_MMAP;
    return_buf.index  = index;

    if (ioctl(fd_, VIDIOC_QBUF, &return_buf) < 0) {
        perror("[V4L2] QBUF");
        return -1;
    }

    // 如果归还的正好是当前持有的缓冲区，清除持有标志
    if (index == current_buf_index_ && buffer_owned_) {
        buffer_owned_ = false;
    }

    return 0;
}

// ================================================================
// stop：停止采集并释放所有资源
// ================================================================
void V4L2Camera::stop() {
    if (!running_ || fd_ < 0) return; 

    // ========== 1. 归还最后一帧（兜底）==========
    if (buffer_owned_ && fd_ >= 0) {
        struct v4l2_buffer return_buf = {0};
        return_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        return_buf.memory = V4L2_MEMORY_MMAP;
        return_buf.index  = current_buf_index_;
        ioctl(fd_, VIDIOC_QBUF, &return_buf);
        buffer_owned_ = false;
    }

    // ========== 2. 停止视频流 ==========
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);

    // ========== 3. 解除 mmap 映射 ==========
    for (int i = 0; i < num_buffers_; i++) {
        if (mmap_addrs_[i]) {
            munmap(mmap_addrs_[i], buffers_[i].length);
            mmap_addrs_[i] = NULL;
        }
    }

    // ========== 4. 释放内存、关闭设备 ==========
    delete[] buffers_;
    buffers_ = nullptr;
    delete[] mmap_addrs_;
    mmap_addrs_ = nullptr;
    
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    
    running_ = false;
}