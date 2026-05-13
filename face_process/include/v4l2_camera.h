// ================================================================
// v4l2_camera.h — V4L2 摄像头采集类（MMAP模式 + DMA-BUF导出）
// ================================================================
#ifndef V4L2_CAMERA_H
#define V4L2_CAMERA_H

#include <string>
#include <cstdint>
#include <linux/videodev2.h>

class V4L2Camera {
public:
    V4L2Camera();
    ~V4L2Camera();

    /**
     * @brief 打开摄像头并开始采集
     * @param device 设备路径，如 "/dev/video21"
     * @param width  图像宽度
     * @param height 图像高度
     * @param fps    帧率
     * @return 0成功，-1失败
     */
    int start(const std::string& device, int width, int height, int fps);

    /**
     * @brief 获取一帧（阻塞等待）
     * @param data   [OUT] 图像数据指针（mmap地址，不释放）
     * @param size   [OUT] 数据大小
     * @param width  [OUT] 图像宽度
     * @param height [OUT] 图像高度
     * @param stride_w [OUT] 行跨度（字节）
     * @param stride_h [OUT] 列跨度
     * @return 0成功，-1失败
     */
    int capture(uint8_t** data, size_t* size, int* width, int* height,
                int* stride_w, int* stride_h);

    /**
     * @brief 导出当前帧的 DMA-BUF fd（零拷贝给DRM）
     *        必须在 capture() 之后、下一帧 capture() 之前调用
     * @return DMA-BUF fd，失败返回 -1
     */
    int export_dma_fd();
    
    // v4l2_camera.h，在 public 区域添加：
    int current_buffer_index() const { return current_buf_index_; }

    // 显式归还指定缓冲区（外部帧管理用）
    int qbuf(int index);

    /**
     * @brief 停止采集并释放资源
     */
    void stop();

    // 禁用拷贝
    V4L2Camera(const V4L2Camera&) = delete;
    V4L2Camera& operator=(const V4L2Camera&) = delete;
    // 允许移动（如果需要）
    // V4L2Camera(V4L2Camera&&);
    // V4L2Camera& operator=(V4L2Camera&&);

private:
    int fd_;                           // 设备文件描述符
    int width_, height_;               // 图像尺寸
    int stride_width_;                 // 行跨度（字节）
    struct v4l2_buffer* buffers_;      // 内核缓冲区信息数组
    void** mmap_addrs_;                // mmap 映射后的用户空间地址
    int num_buffers_;                  // 缓冲区数量
    bool running_;                     // 采集状态

    int current_buf_index_;            // 当前持有的缓冲区索引
    bool buffer_owned_;                // 是否持有一个未归还的缓冲区
};

#endif // V4L2_CAMERA_H