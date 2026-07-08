#ifndef RECOGNITION_ENGINE_HPP
#define RECOGNITION_ENGINE_HPP

#include <chrono>
#include <cstring>
#include <stdio.h>
#include "face_module.h"
#include "v4l2_camera.h"
#include "display_sender.hpp"
#include "ipc/ipc_socket.h"
#include <opencv2/opencv.hpp>

extern volatile bool g_running;

/**
 * @brief 推理结果
 */
struct SessionResult {
    bool   success;        // 是否识别成功
    int    feature_id;     // 人脸特征ID
    float  similarity;     // 相似度
    int    face_rect[4];   // 人脸框坐标 [x,y,w,h]
};

/**
 * @brief 推理引擎：采集 + 人脸识别 + 异步推帧
 *
 * 每采集一帧：
 *   1. sender_.push() — 立即 export fd 并投递到推帧队列（非阻塞）
 *   2. face.search()  — 用 mmap 虚拟地址做推理
 *   3. cam.qbuf()     — 归还 V4L2 buffer
 *
 * 推帧和推理完全并行，互不影响。
 */
class RecognitionEngine {
public:
    RecognitionEngine(FaceModule& face, V4L2Camera& cam, int disp_fd)
        : face_(face), cam_(cam), sender_(cam, disp_fd) {}

    /**
     * @brief 执行一次识别/录入会话
     * @param mode  0=识别（连续帧确认），3=录入（单帧提取）
     * @param out   输出结果
     * @return true 成功
     */
    bool run(int mode, SessionResult& out) {
        uint8_t* data = nullptr;
        size_t   size = 0;
        int w = 0, h = 0, sw = 0, sh = 0;

        int64_t last_id = -1;
        int confirm_count = 0;
        const int CONFIRM_FRAMES = 5;
        auto start_t = std::chrono::steady_clock::now();
        int buf_index = -1;
        int rga_fd    = -1;   // ★ 为 RGA 导出的 DMA-BUF fd，每帧用完即 close

        const char* task = (mode == 3) ? "录入" : "识别";
        printf("[Engine] 开始%s,3秒超时...\n", task);

        // ★ 释放当前帧资源的内联 lambda（close fd + qbuf + 清标志）
        auto release_frame = [&]() {
            if (rga_fd >= 0) { close(rga_fd); rga_fd = -1; }
            if (buf_index >= 0) { cam_.qbuf(buf_index); buf_index = -1; }
        };

        while (g_running) {
            // ---- 超时 ----
            auto elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - start_t).count();
            if (elapsed >= 3.0f && confirm_count == 0) {
                printf("[Engine] 3秒超时\n");
                release_frame();
                return false;
            }

            // ---- 归还上一帧 ----
            release_frame();

            // ---- 采集 ----
            if (cam_.capture(&data, &size, &w, &h, &sw, &sh) != 0) {
                usleep(1000); continue;
            }
            buf_index = cam_.current_buffer_index();

            // ============================================
            // ★ 异步推帧：立即 export fd → 队列 → 线程发送
            //    在 qbuf 之前执行，保证 buffer_index 有效
            // ============================================
            sender_.push(buf_index, w, h, sw, sh, size,
                         out.face_rect, out.feature_id > 0);

            // ---- 推理 ----
            // ★ 为 RGA 导出 DMA-BUF fd（比 virt_addr 路径更稳定，
            //   避免 V4L2 mmap 页面被内核回收导致 RGA "Invalid argument"）
            rga_fd = cam_.export_dma_fd();
            image_buffer_t img;
            img.virt_addr    = data;
            img.width        = w;    img.height       = h;
            img.width_stride = sw;   img.height_stride = sh;
            img.format       = IMAGE_FORMAT_YUYV422;
            img.size         = size;
            img.fd           = rga_fd;   // ≥0 时 face_module 走 wrapbuffer_fd 路径

            if (mode == 3) {
                int fid; bool dup;
                int ret = face_.extract_dedup(&img, fid, dup);
                release_frame();
                if (ret == 0) { out.success = true; out.feature_id = fid; }
                return (ret == 0);
            }

            FaceResult result;
            if (face_.search(&img, result) == 0 && result.valid) {
                if ((int64_t)result.id == last_id) {
                    confirm_count++;
                    if (confirm_count >= CONFIRM_FRAMES) {
                        out.success      = true;
                        out.feature_id   = (int)result.id;
                        out.similarity   = result.similarity;
                        out.face_rect[0] = result.x; out.face_rect[1] = result.y;
                        out.face_rect[2] = result.w; out.face_rect[3] = result.h;
                        printf("[Engine] 识别成功 id=%d Sim:%.4f\n",
                               out.feature_id, result.similarity);
                        release_frame();
                        return true;
                    }
                } else {
                    last_id = result.id;
                    confirm_count = 1;
                }
            } else {
                last_id = -1;
                confirm_count = 0;
            }
        }
        release_frame();
        return false;
    }

    // ============================================================
    // recognize_from_stream：从 RTSP/本地视频流拉帧识别
    // ----------------------------------------------------------------
    // 用于 ESP32 流程：
    //   1. OpenCV cv::VideoCapture 打开 rtsp_url（也支持本地文件路径）
    //   2. 循环读帧（3 秒超时），每帧转 BGR 后调用 face_.search_bgr()
    //   3. 连续 CONFIRM_FRAMES 帧识别同一人 → 成功
    //
    // 不占用 V4L2Camera（USB 摄像头），与 qt_command_handler 互斥访问 FaceModule
    //
    // @param ev       ESP32 识别事件（含 rtsp_url + task 信息）
    // @param out_name [OUT] 识别到的人名
    // @param out_id   [OUT] 识别到的 feature_id
    // @return true=识别成功，false=失败/超时
    // ============================================================
    bool recognize_from_stream(const Esp32RecognizeEvent& ev,
                               char* out_name, int& out_id)
    {
        printf("[Engine] 开始流识别 task=%d %s\n", ev.task_id, ev.rtsp_url);

        cv::VideoCapture cap(ev.rtsp_url, cv::CAP_FFMPEG);
        if (!cap.isOpened()) {
            fprintf(stderr, "[Engine] 无法打开视频流: %s\n", ev.rtsp_url);
            return false;
        }

        int64_t last_id = -1;
        int confirm_count = 0;
        const int CONFIRM_FRAMES = 3;   // 流识别帧数门槛比 USB 低（3帧）
        auto start_t = std::chrono::steady_clock::now();
        const float TIMEOUT_SEC = 8.0f; // 流识别给 8 秒（首次打开 RTSP 较慢）

        cv::Mat frame;
        while (g_running) {
            auto elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - start_t).count();
            if (elapsed >= TIMEOUT_SEC && confirm_count == 0) {
                printf("[Engine] 流识别 %.0fs 超时\n", TIMEOUT_SEC);
                cap.release();
                return false;
            }

            if (!cap.read(frame) || frame.empty()) {
                // 流结束/读帧失败：若已识别过则返回，否则重试一次
                usleep(30000);
                if (!cap.read(frame) || frame.empty()) {
                    printf("[Engine] 流读取结束\n");
                    break;
                }
            }

            // BGR Mat → search_bgr（要求连续内存，OpenCV Mat 默认连续）
            if (!frame.isContinuous()) {
                frame = frame.clone();
            }

            FaceResult result;
            if (face_.search_bgr(frame.data, frame.cols, frame.rows, result) == 0
                && result.valid) {
                if ((int64_t)result.id == last_id) {
                    confirm_count++;
                    if (confirm_count >= CONFIRM_FRAMES) {
                        out_id = (int)result.id;
                        printf("[Engine] 流识别成功 task=%d id=%d Sim:%.4f\n",
                               ev.task_id, out_id, result.similarity);
                        cap.release();
                        return true;
                    }
                } else {
                    last_id = result.id;
                    confirm_count = 1;
                }
            } else {
                last_id = -1;
                confirm_count = 0;
            }
        }

        cap.release();
        return false;
    }

private:
    FaceModule&    face_;
    V4L2Camera&    cam_;
    DisplaySender  sender_;    // 独立推帧线程
};

#endif // RECOGNITION_ENGINE_HPP