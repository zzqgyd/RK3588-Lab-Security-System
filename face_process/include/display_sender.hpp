#ifndef DISPLAY_SENDER_HPP
#define DISPLAY_SENDER_HPP

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include "v4l2_camera.h"
#include "ipc/ipc_socket.h"

extern volatile bool g_running;

/**
 * @brief 推帧数据包
 *
 * push 时立即调用 export_dma_fd 导出 fd，
 * 线程只负责 send + close，不再依赖 buffer_index。
 */
struct DisplayPacket {
    int    dma_fd;         // 已导出的 DMA-BUF fd
    int    width;          // 图像宽度
    int    height;         // 图像高度
    int    stride_w;       // 行跨度
    int    stride_h;       // 列跨度
    size_t size;           // 数据大小
    int    face_rect[4];   // 人脸框坐标 [x,y,w,h]
    bool   has_face;       // 是否有人脸
};

/**
 * @brief 显示发送器 — 独立线程异步推帧
 *
 * 用法：
 *   DisplaySender sender(cam, disp_fd);
 *   sender.push(buf_index, w, h, sw, sh, size, face_rect, has_face);
 *
 * 关键设计：
 *   - push 在调用者线程中立即执行 export_dma_fd
 *   - 导出成功后把 fd 放入队列
 *   - 独立线程从队列取 fd，sendmsg 发送，close
 *   - push 不阻塞，sendmsg 也不影响推理
 *   - fd 生命周期由 DMA-BUF 内核引用计数管理
 */
class DisplaySender {
public:
    /**
     * @param cam      V4L2 摄像头，用于 export_dma_fd
     * @param disp_fd  显示进程 Socket，-1 表示不发送
     */
    DisplaySender(V4L2Camera& cam, int disp_fd)
        : cam_(cam), disp_fd_(disp_fd), running_(true)
    {
        thread_ = std::thread(&DisplaySender::loop, this);
    }

    ~DisplaySender() {
        running_ = false;
        cv_.notify_all();                          // 唤醒可能在 wait 的线程
        if (thread_.joinable()) thread_.join();    // 等待线程退出
    }

    /**
     * @brief 非阻塞推帧（在 V4L2 DQBUF 之后、qbuf 之前调用）
     *
     * @param buffer_index  V4L2 缓冲区索引
     * @param w, h          帧宽高
     * @param sw, sh        stride
     * @param sz            帧大小
     * @param face          人脸框 [x,y,w,h]，无脸时全 0
     * @param has_face      是否有人脸
     */
    void push(int buffer_index, int w, int h, int sw, int sh, size_t sz,
              const int face[4], bool has_face) {

        // ★ 立即导出 DMA-BUF fd
        //    此时 buffer_index 一定有效（在 qbuf 之前调用）
        int fd = cam_.export_dma_fd();
        if (fd < 0) return;       // 驱动不支持则丢弃本帧

        DisplayPacket pkt;
        pkt.dma_fd    = fd;       // 线程只管 send + close
        pkt.width     = w;
        pkt.height    = h;
        pkt.stride_w  = sw;
        pkt.stride_h  = sh;
        pkt.size      = sz;
        pkt.has_face  = has_face;
        if (has_face) {
            memcpy(pkt.face_rect, face, sizeof(pkt.face_rect));
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            // 队列最多保留 3 帧，超过则丢弃最旧的
            if (queue_.size() >= 3) {
                close(queue_.front().dma_fd);    // 丢弃帧的 fd 也要释放
                queue_.pop();
            }
            queue_.push(pkt);
        }
        cv_.notify_one();                         // 唤醒推帧线程
    }

private:
    /**
     * @brief 推帧线程主循环
     *        从队列取已导出的 fd → sendmsg → close
     */
    void loop() {
        while (running_ || !queue_.empty()) {
            DisplayPacket pkt;

            {
                std::unique_lock<std::mutex> lk(mutex_);
                cv_.wait(lk, [this]{              // 阻塞等待队列非空或退出
                    return !queue_.empty() || !running_;
                });

                if (queue_.empty()) break;        // 退出且队列空 → 线程结束
                pkt = queue_.front();
                queue_.pop();
            }

            // 全局退出或显示进程未连接 → 释放 fd，跳过
            if (!g_running || disp_fd_ < 0 || pkt.dma_fd < 0) {
                if (pkt.dma_fd >= 0) close(pkt.dma_fd);
                continue;
            }

            // ---- 构建 FrameMeta（通过 Socket 发送的元数据）----
            FrameMeta meta;
            memset(&meta, 0, sizeof(meta));
            meta.width           = pkt.width;
            meta.height          = pkt.height;
            meta.stride_w        = pkt.stride_w;
            meta.stride_h        = pkt.stride_h;
            meta.format          = 1;              // YUYV
            meta.size            = pkt.size;
            meta.stream_id       = -1;             // USB 摄像头
            meta.detection_count = 0;

            if (pkt.has_face) {
                meta.boxes[0].x = pkt.face_rect[0];
                meta.boxes[0].y = pkt.face_rect[1];
                meta.boxes[0].w = pkt.face_rect[2];
                meta.boxes[0].h = pkt.face_rect[3];
                meta.detection_count = 1;
            }

            // ---- 发送：元数据 + DMA-BUF fd（SCM_RIGHTS 零拷贝）----
            ipc_sock_send_frame(disp_fd_, &meta, pkt.dma_fd);

            // 发送完毕，关闭本进程的 fd 副本
            // 显示进程那边有自己的 fd 引用，物理内存不会释放
            close(pkt.dma_fd);
        }
    }

    V4L2Camera&            cam_;               // 用于 export_dma_fd
    int                    disp_fd_;           // 显示进程 Socket
    std::atomic<bool>      running_;           // 线程运行标志
    std::thread            thread_;            // 推帧线程

    std::queue<DisplayPacket> queue_;          // 待发送帧队列
    std::mutex              mutex_;            // 保护队列
    std::condition_variable cv_;               // 阻塞等新帧
};

#endif // DISPLAY_SENDER_HPP