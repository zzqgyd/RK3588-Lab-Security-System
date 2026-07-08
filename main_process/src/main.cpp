/**
 * @file main.cpp
 * @brief 主程序入口 - 集成所有模块
 * 
 * 启动流程：
 *   1. 加载配置
 *   2. 创建帧池
 *   3. 创建队列系统
 *   4. 初始化RKNN模型
 *   5. 创建Worker线程池
 *   6. 创建录像线程池
 *   7. 启动解码器
 *   8. 主循环（推帧给QT）
 */

#include <signal.h>
#include <gst/gst.h>
#include <unistd.h>
#include <cstdio>
#include <memory>
#include <vector>
#include <chrono>

#include "gst_decoder.h"
#include "yolov5_infer.h"
#include "common.h"
#include "queue_manager.hpp"
#include "mpmc_queue.hpp"
#include "scheduler.hpp"
#include "worker_pool.hpp"
#include "frame_pool.hpp"
#include "config.h"
#include "recorder_queue.hpp"
#include "ipc/ipc_socket.h"

// ================================================================
// 全局变量
// ================================================================
static volatile bool g_running = true;

QueueManager*        g_queue_manager    = nullptr;
MPMCQueue<FramePtr>* g_inference_queue  = nullptr;
Scheduler*           g_scheduler        = nullptr;
WorkerPool*          g_worker_pool      = nullptr;

base_model_context_t g_base_ctx = {0};
worker_context_t     g_worker_ctxs[WORKER_NUM] = {{0}};
AppConfig            g_cfg = {0};

std::vector<std::unique_ptr<FramePool>> g_frame_pools;

// Phase2 函数声明（在 worker_pool.cpp 中实现）
extern void phase2_init(const char* db_path);
extern void phase2_deinit();

// ================================================================
// 信号处理
// ================================================================
static void sigint_handler(int sig) {
    static int count = 0;
    count++;
    if (count == 1) {
        g_running = false;
        printf("\n[Main] Shutting down...\n");
    } else {
        printf("\n[Main] Force exit\n");
        exit(1);
    }
}

// ================================================================
// 加载配置文件
// ================================================================
int load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[Main] Config file %s not found\n", filename);
        return -1;
    }

    char line[CONFIG_LINE_MAX];
    g_cfg.channel_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;

        if (strstr(line, "path ="))
            sscanf(line, "path = %s", g_cfg.model_path);

        if (strstr(line, "rtsp =")) {
            ChannelContext *ch = &g_cfg.channels[g_cfg.channel_count];
            ch->id = g_cfg.channel_count;
            sscanf(line, "rtsp = %s", ch->rtsp);
            g_cfg.channel_count++;
        }
    }
    fclose(f);

    printf("[Main] Config: %d channels, model=%s\n",
           g_cfg.channel_count, g_cfg.model_path);
    return (g_cfg.channel_count > 0) ? 0 : -1;
}

// ================================================================
// 主函数
// ================================================================
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    gst_init(NULL, NULL);
    
    // 1. 加载配置
    if (load_config("../../config/config.ini") < 0) return -1;

    // 2. 创建帧池（每路独立）
    for (int i = 0; i < g_cfg.channel_count; i++) {
        auto pool = std::make_unique<FramePool>(FRAMES_PER_STREAM);
        g_cfg.channels[i].frame_pool = pool.get();
        g_frame_pools.push_back(std::move(pool));
    }

    // 3. 创建队列系统
    g_queue_manager   = new QueueManager();
    g_inference_queue = new MPMCQueue<FramePtr>(MPMC_CAPACITY);
    g_scheduler       = new Scheduler(g_queue_manager, g_inference_queue, g_cfg.channel_count);
    g_scheduler->start();

    // 4. 初始化RKNN模型
    if (yolov5_init_base(&g_base_ctx, g_cfg.model_path) < 0) return -1;
    for (int i = 0; i < WORKER_NUM; i++) {
        yolov5_dup_worker_context(&g_base_ctx, &g_worker_ctxs[i], i);
    }

    // 5. 创建Worker线程池
    g_worker_pool = new WorkerPool(WORKER_NUM, g_inference_queue,
                                   g_worker_ctxs, g_cfg.channels, g_cfg.channel_count);
    g_worker_pool->start();

    // 6. Phase2初始化（ROI配置 + 状态机 + 录像线程池 + IPC + 数据库）
    phase2_init("../../config/records.db");

    // 7. 连接QT UI
    int qt_sock = ipc_sock_client_connect(SOCK_PATH_MAIN_QT);
    if (qt_sock < 0) {
        printf("[Main] QT UI not connected, frame push disabled\n");
    }

    // 8. 启动解码器（每路一个GStreamer pipeline）
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        ch->decoder = gst_decoder_create(ch->rtsp, NULL, (void*)(intptr_t)i);
        if (ch->decoder) {
            gst_decoder_start(ch->decoder);
            printf("[Main] Stream %d decoder started: %s\n", i, ch->rtsp);
        } else {
            printf("[Main] Stream %d decoder failed\n", i);
        }
    }

    printf("[Main] === System Running ===\n");
    printf("[Main] Channels: %d, Workers: %d, Recorders: %d\n", 
           g_cfg.channel_count, WORKER_NUM, g_cfg.channel_count);

    // ============================================================
    // 主循环：推帧给QT UI
    // ============================================================
    auto last_print_time = std::chrono::steady_clock::now();
    const double PRINT_INTERVAL_SEC = 5.0;
    
    while (g_running) {
        g_main_context_iteration(NULL, FALSE);

        // 轮询 ROI 重载命令（非阻塞，由 QT 端保存 ROI 后触发）
        phase2_poll_roi_reload();

        // 轮询 QT → 主进程 命令（手动断电等，非阻塞）
        phase2_poll_qt_main();

        // 轮询 device_process 连接（非阻塞 accept）
        phase2_poll_device();

        // 轮询 face_process 回传的 ESP32 识别结果（非阻塞）
        phase2_poll_face_result();

        // 推帧给QT UI
        if (qt_sock >= 0 && g_cfg.channel_count > 0) {
            for (int i = 0; i < g_cfg.channel_count; i++) {
                FramePtr f = g_cfg.channels[i].get_frame();
                if (!f) continue;

                // 只推DMA-BUF帧
                if (!f->is_dmabuf || f->img.fd < 0) continue;

                // 获取检测框
                DetectionBox boxes[MAX_DETECTIONS];
                int box_cnt = 0;
                g_cfg.channels[i].get_boxes(boxes, box_cnt);

                // 构建帧元数据
                FrameMeta meta;
                memset(&meta, 0, sizeof(meta));
                meta.width      = f->img.width;
                meta.height     = f->img.height;
                meta.stride_w   = f->img.width_stride;
                meta.stride_h   = f->img.height_stride;
                meta.format     = 0;  // NV12
                meta.size       = f->img.size;
                meta.pts        = f->pts;
                meta.stream_id  = i;
                meta.detection_count = box_cnt;
                for (int j = 0; j < box_cnt && j < MAX_DETECTIONS; j++) {
                    meta.boxes[j] = boxes[j];
                }

                // 填充该路设备占用状态（Qt 用于 ROI 框颜色/文字）
                phase2_fill_device_status(&meta, i);

                // 发送
                ipc_sock_send_frame(qt_sock, &meta, f->img.fd);
            }
        }

        // 定期打印统计信息
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - last_print_time).count() >= PRINT_INTERVAL_SEC) {
            // 可以添加统计输出
            last_print_time = now;
        }

        usleep(30000);  // ~33fps
    }

    // ============================================================
    // 清理资源
    // ============================================================
    printf("\n[Main] Cleaning up...\n");
    
    // 1. 停止解码器
    for (int i = 0; i < g_cfg.channel_count; i++) {
        if (g_cfg.channels[i].decoder) {
            gst_decoder_stop(g_cfg.channels[i].decoder);
            gst_decoder_destroy(g_cfg.channels[i].decoder);
        }
    }
    
    // 2. 停止Worker和Scheduler
    if (g_worker_pool) {
        g_worker_pool->stop();
        delete g_worker_pool;
    }
    if (g_scheduler) {
        g_scheduler->stop();
        delete g_scheduler;
    }
    if (g_inference_queue) delete g_inference_queue;

    // 3. Phase2清理（包含录像线程池）
    phase2_deinit();

    // 4. 释放RKNN资源
    for (int i = 0; i < WORKER_NUM; i++) {
        yolov5_release_worker(&g_worker_ctxs[i]);
    }
    yolov5_release_base(&g_base_ctx);

    // 5. 其他清理
    if (g_queue_manager) delete g_queue_manager;
    g_frame_pools.clear();
    
    if (qt_sock >= 0) {
        ipc_sock_close(qt_sock, NULL);
    }

    printf("[Main] Done\n");
    return 0;
}