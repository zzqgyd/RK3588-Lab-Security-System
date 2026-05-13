#ifndef RECOGNITION_ENGINE_HPP
#define RECOGNITION_ENGINE_HPP

#include <chrono>
#include <cstring>
#include <stdio.h>
#include "face_module.h"
#include "v4l2_camera.h"
#include "display_sender.hpp"

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

        const char* task = (mode == 3) ? "录入" : "识别";
        printf("[Engine] 开始%s,3秒超时...\n", task);

        while (g_running) {
            // ---- 超时 ----
            auto elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - start_t).count();
            if (elapsed >= 3.0f && confirm_count == 0) {
                printf("[Engine] 3秒超时\n");
                if (buf_index >= 0) cam_.qbuf(buf_index);
                return false;
            }

            // ---- 归还上一帧 ----
            if (buf_index >= 0) {
                cam_.qbuf(buf_index);
                buf_index = -1;
            }

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
            image_buffer_t img;
            img.virt_addr    = data;
            img.width        = w;    img.height       = h;
            img.width_stride = sw;   img.height_stride = sh;
            img.format       = IMAGE_FORMAT_YUYV422;
            img.size         = size;
            img.fd           = -1;

            if (mode == 3) {
                int fid; bool dup;
                int ret = face_.extract_dedup(&img, fid, dup);
                cam_.qbuf(buf_index);
                buf_index = -1;
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
                        if (buf_index >= 0) { cam_.qbuf(buf_index); buf_index = -1; }
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
        if (buf_index >= 0) cam_.qbuf(buf_index);
        return false;
    }

private:
    FaceModule&    face_;
    V4L2Camera&    cam_;
    DisplaySender  sender_;    // 独立推帧线程
};

#endif // RECOGNITION_ENGINE_HPP