// ===== 文件: src/demo.cpp =====
// Step 1 修改版本

/**
 * @file demo.cpp
 * @brief 多路RTSP视频流 + YOLO推理 + 2x2显示主程序
 */

#include <signal.h>
#include <gst/gst.h>
#include <unistd.h>
#include <iostream>
#include <cstdio>
#include <memory>
#include <vector>

// GStreamer 模块
#include "gst_decoder.h"
#include "gst_display.h"

// 推理模块
#include "yolov5_infer.h"
#include "image_drawing.h"
#include "common.h"

// RGA 头文件
#include "im2d.h"
#include "RgaUtils.h"

// 队列模块
#include "queue_manager.hpp"
#include "mpmc_queue.hpp"
#include "scheduler.hpp"
#include "worker_pool.hpp"
#include "frame_pool.hpp"

// 配置模块
#include "config.h"

// ============================================================================
// 宏定义
// ============================================================================
#define CONFIG_LINE_MAX    256
#define CANVAS_W           1920   // 根据实际显示调整
#define CANVAS_H           1080
#define WORKER_NUM         3
#define FRAMES_PER_STREAM  3

// ============================================================================
// 全局变量
// ============================================================================
static volatile bool g_running = true;

QueueManager* g_queue_manager = nullptr;
MPMCQueue<FramePtr>* g_inference_queue = nullptr;
Scheduler* g_scheduler = nullptr;
WorkerPool* g_worker_pool = nullptr;

base_model_context_t g_base_ctx = {0};
worker_context_t g_worker_ctxs[WORKER_NUM] = {{0}};
AppConfig g_cfg = {0};

// 帧池容器（每路独立）
std::vector<std::unique_ptr<FramePool>> g_frame_pools;

void sigint_handler(int sig) {
    g_running = false;
    printf("\n[Main] Exit signal received...\n");
}

int load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[Main] Failed to open config: %s\n", filename);
        return -1;
    }

    char line[CONFIG_LINE_MAX];
    g_cfg.channel_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        
        if (strstr(line, "path =")) {
            sscanf(line, "path = %s", g_cfg.model_path);
        }
        
        if (strstr(line, "rtsp =")) {
            ChannelContext *ch = &g_cfg.channels[g_cfg.channel_count];
            ch->id = g_cfg.channel_count;
            sscanf(line, "rtsp = %s", ch->rtsp);
            g_cfg.channel_count++;
        }
    }
    fclose(f);
    
    printf("[Main] Config loaded: %d channels, model=%s\n", 
           g_cfg.channel_count, g_cfg.model_path);
    return 0;
}


// ===== 文件: demo.cpp 中的 composite_2x2_rga_nv12 函数 =====
void composite_2x2_rga_nv12(uint8_t *canvas, ChannelContext *channels, int count)
{
    // 清空画布
    memset(canvas, 0, CANVAS_W * CANVAS_H);
    memset(canvas + CANVAS_W * CANVAS_H, 128, CANVAS_W * CANVAS_H / 2);
    
    rga_buffer_t dst = wrapbuffer_virtualaddr(canvas, CANVAS_W, CANVAS_H, RK_FORMAT_YCbCr_420_SP);
    
    for (int i = 0; i < count; i++) {
        FramePtr frame = channels[i].get_frame();
        if (!frame || !frame->img.virt_addr) continue;
        
        // 计算目标区域
        int cols = 4;  // 4 列
        int rows = 2;  // 2 行
        int cell_w = CANVAS_W / cols;
        int cell_h = CANVAS_H / rows;
        
        float scale_w = (float)cell_w / frame->img.width;
        float scale_h = (float)cell_h / frame->img.height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        
        int draw_w = ((int)(frame->img.width * scale)) & ~1;
        int draw_h = ((int)(frame->img.height * scale)) & ~1;
        int ox = ((cell_w - draw_w) / 2) & ~1;
        int oy = ((cell_h - draw_h) / 2) & ~1;
        
        int dst_x = (i % 2) * cell_w + ox;
        int dst_y = (i / 2) * cell_h + oy;
        
        // ===== RGA 封装：优先使用 fd（零拷贝）=====
        rga_buffer_t src;
        if (frame->is_dmabuf && frame->img.fd >= 0) {
            // DMA-BUF 模式：使用 fd，零拷贝
            src = wrapbuffer_fd(frame->img.fd,
                                frame->img.width, 
                                frame->img.height,
                                RK_FORMAT_YCbCr_420_SP,
                                frame->img.width_stride, 
                                frame->img.height_stride);
        } else {
            // 回退模式：使用虚拟地址
            src = wrapbuffer_virtualaddr(frame->img.virt_addr,
                                         frame->img.width, 
                                         frame->img.height,
                                         RK_FORMAT_YCbCr_420_SP,
                                         frame->img.width_stride, 
                                         frame->img.height_stride);
        }
        
        im_rect src_rect = {0, 0, frame->img.width, frame->img.height};
        im_rect dst_rect = {dst_x, dst_y, draw_w, draw_h};
        
        // RGA 缩放合成
        IM_STATUS ret = improcess(src, dst, {}, src_rect, dst_rect, {}, 0);
        if (ret != IM_STATUS_SUCCESS) {
            printf("[Display] RGA improcess failed for stream %d, ret=%d\n", 
                   frame->stream_id, ret);
        }
    }
}

// ===== 文件: demo.cpp - composite_2x4_rga_nv12 =====
void composite_2x4_rga_nv12(uint8_t *canvas, ChannelContext *channels, int count)
{
    // 清空画布
    memset(canvas, 0, CANVAS_W * CANVAS_H);
    memset(canvas + CANVAS_W * CANVAS_H, 128, CANVAS_W * CANVAS_H / 2);
    
    rga_buffer_t dst = wrapbuffer_virtualaddr(canvas, CANVAS_W, CANVAS_H, RK_FORMAT_YCbCr_420_SP);
    
    int cols = 4;  // 4 列
    int rows = 2;  // 2 行
    int cell_w = CANVAS_W / cols;
    int cell_h = CANVAS_H / rows;
    
    for (int i = 0; i < count && i < 8; i++) {
        FramePtr frame = channels[i].get_frame();
        if (!frame || !frame->img.virt_addr) continue;
        
        // 计算缩放
        float scale_w = (float)cell_w / frame->img.width;
        float scale_h = (float)cell_h / frame->img.height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        
        int draw_w = ((int)(frame->img.width * scale)) & ~1;
        int draw_h = ((int)(frame->img.height * scale)) & ~1;
        int ox = ((cell_w - draw_w) / 2) & ~1;
        int oy = ((cell_h - draw_h) / 2) & ~1;
        
        int col = i % cols;
        int row = i / cols;
        int dst_x = col * cell_w + ox;
        int dst_y = row * cell_h + oy;
        
        // RGA 封装
        rga_buffer_t src;
        if (frame->is_dmabuf && frame->img.fd >= 0) {
            src = wrapbuffer_fd(frame->img.fd, frame->img.width, frame->img.height,
                                RK_FORMAT_YCbCr_420_SP,
                                frame->img.width_stride, frame->img.height_stride);
        } else {
            src = wrapbuffer_virtualaddr(frame->img.virt_addr,
                                         frame->img.width, frame->img.height,
                                         RK_FORMAT_YCbCr_420_SP);
        }
        
        im_rect src_rect = {0, 0, frame->img.width, frame->img.height};
        im_rect dst_rect = {dst_x, dst_y, draw_w, draw_h};
        
        improcess(src, dst, {}, src_rect, dst_rect, {}, 0);
    }
}

// ===== 文件: demo.cpp - composite_4x4_rga_nv12 =====
void composite_4x4_rga_nv12(uint8_t *canvas, ChannelContext *channels, int count)
{
    // 清空画布
    memset(canvas, 0, CANVAS_W * CANVAS_H);
    memset(canvas + CANVAS_W * CANVAS_H, 128, CANVAS_W * CANVAS_H / 2);
    
    rga_buffer_t dst = wrapbuffer_virtualaddr(canvas, CANVAS_W, CANVAS_H, RK_FORMAT_YCbCr_420_SP);
    
    int cols = 4;
    int rows = 4;
    int cell_w = CANVAS_W / cols;   // 1920 / 4 = 480
    int cell_h = CANVAS_H / rows;   // 1080 / 4 = 270
    
    for (int i = 0; i < count && i < 16; i++) {
        FramePtr frame = channels[i].get_frame();
        if (!frame || !frame->img.virt_addr) continue;
        
        // 计算缩放
        float scale_w = (float)cell_w / frame->img.width;
        float scale_h = (float)cell_h / frame->img.height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        
        int draw_w = ((int)(frame->img.width * scale)) & ~1;
        int draw_h = ((int)(frame->img.height * scale)) & ~1;
        
        // 确保对齐
        if (draw_w < 4) draw_w = 4;
        if (draw_h < 2) draw_h = 2;
        
        int ox = ((cell_w - draw_w) / 2) & ~1;
        int oy = ((cell_h - draw_h) / 2) & ~1;
        
        int col = i % cols;
        int row = i / cols;
        int dst_x = col * cell_w + ox;
        int dst_y = row * cell_h + oy;
        
        // RGA 封装
        rga_buffer_t src;
        if (frame->is_dmabuf && frame->img.fd >= 0) {
            src = wrapbuffer_fd(frame->img.fd, 
                                frame->img.width, frame->img.height,
                                RK_FORMAT_YCbCr_420_SP,
                                frame->img.width_stride, frame->img.height_stride);
        } else {
            src = wrapbuffer_virtualaddr(frame->img.virt_addr,
                                         frame->img.width, frame->img.height,
                                         RK_FORMAT_YCbCr_420_SP,
                                         frame->img.width_stride, frame->img.height_stride);
        }
        
        im_rect src_rect = {0, 0, frame->img.width, frame->img.height};
        im_rect dst_rect = {dst_x, dst_y, draw_w, draw_h};
        
        IM_STATUS ret = improcess(src, dst, {}, src_rect, dst_rect, {}, 0);
        if (ret != IM_STATUS_SUCCESS) {
            // 16 路时 RGA 可能繁忙，忽略偶发错误
            static int rga_err_count = 0;
            if (++rga_err_count % 100 == 0) {
                printf("[Display] RGA error count: %d\n", rga_err_count);
            }
        }
    }
}

// ==============================
// 主函数
// ==============================
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    gst_init(NULL, NULL);
    
    if (load_config("./config/config.ini") < 0) return -1;

    // ===== Step 1 新增：为每路流创建 FramePool =====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        auto pool = std::make_unique<FramePool>(FRAMES_PER_STREAM);
        g_cfg.channels[i].frame_pool = pool.get();
        g_frame_pools.push_back(std::move(pool));
        printf("[Main] FramePool created for stream %d, capacity=%d\n", i, FRAMES_PER_STREAM);
    }

    // ===== 创建队列管理器 =====
    g_queue_manager = new QueueManager();
    printf("[Main] QueueManager created\n");

    // ===== 初始化基础模型 =====
    int ret = yolov5_init_base(&g_base_ctx, g_cfg.model_path);
    if (ret < 0) {
        printf("[Main] Failed to init base model\n");
        delete g_queue_manager;
        return -1;
    }
    printf("[Main] Base model initialized\n");
    
    // ===== 复制 Worker 上下文 =====
    for (int i = 0; i < WORKER_NUM; i++) {
        ret = yolov5_dup_worker_context(&g_base_ctx, &g_worker_ctxs[i],i);
        if (ret < 0) {
            printf("[Main] Failed to dup worker context %d\n", i);
            for (int j = 0; j < i; j++) {
                yolov5_release_worker(&g_worker_ctxs[j]);
            }
            yolov5_release_base(&g_base_ctx);
            delete g_queue_manager;
            return -1;
        }
    }
    printf("[Main] Model initialized: 1 base + %d workers\n", WORKER_NUM);

    // ===== 创建 MPMC 队列 =====
    g_inference_queue = new MPMCQueue<FramePtr>(256);
    printf("[Main] MPMC queue created (capacity: 256)\n");

    // ===== 创建并启动调度器 =====
    g_scheduler = new Scheduler(g_queue_manager, g_inference_queue, g_cfg.channel_count);
    g_scheduler->start();
    printf("[Main] Scheduler started\n");

    // ===== 创建并启动 Worker 池 =====
    g_worker_pool = new WorkerPool(
        WORKER_NUM,
        g_inference_queue,
        g_worker_ctxs,
        g_cfg.channels,
        g_cfg.channel_count
    );
    g_worker_pool->start();
    printf("[Main] WorkerPool started (%d workers)\n", WORKER_NUM);

    // ===== 初始化显示 =====
    size_t canvas_size = CANVAS_W * CANVAS_H * 3 / 2;
    uint8_t *canvas = (uint8_t *)malloc(canvas_size);
    if (!canvas) {
        printf("[Main] Failed to allocate canvas\n");
        // 清理...
        return -1;
    }
    
    if (gst_display_init(CANVAS_W, CANVAS_H) < 0) {
        printf("[Main] Failed to init display\n");
        free(canvas);
        // 清理...
        return -1;
    }
    printf("[Main] Display initialized (%dx%d)\n", CANVAS_W, CANVAS_H);

    // ===== 创建并启动解码器 =====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        ch->decoder = gst_decoder_create(ch->rtsp, NULL, (void*)(intptr_t)i);
        if (!ch->decoder) {
            printf("[Main] Failed to create decoder for stream %d\n", i);
            // 清理...
            return -1;
        }
        gst_decoder_start(ch->decoder);
        printf("[Main] Decoder started for stream %d: %s\n", i, ch->rtsp);
    }

    // ===== 主循环 =====
    printf("\n[Main] === System running, press Ctrl+C to exit ===\n\n");
    while (g_running) {
        g_main_context_iteration(NULL, FALSE);
        // 降低合成频率，避免过度占用 CPU
        static int frame_count = 0;
        if (++frame_count % 2 == 0) {  // 每2帧合成一次
            // composite_2x2_rga_nv12(canvas, g_cfg.channels, g_cfg.channel_count);
            // composite_2x4_rga_nv12(canvas, g_cfg.channels, g_cfg.channel_count);
            composite_4x4_rga_nv12(canvas, g_cfg.channels, g_cfg.channel_count);
            gst_display_push_nv12(CANVAS_W, CANVAS_H, canvas, canvas_size);
        }
        usleep(10000);
    }

    // ===== 清理资源 =====
    printf("\n[Main] Shutting down...\n");

    // 停止 Worker 池
    if (g_worker_pool) {
        g_worker_pool->stop();
        delete g_worker_pool;
        g_worker_pool = nullptr;
    }

    // 停止调度器
    if (g_scheduler) {
        g_scheduler->stop();
        delete g_scheduler;
        g_scheduler = nullptr;
    }

    // 释放 MPMC 队列
    if (g_inference_queue) {
        delete g_inference_queue;
        g_inference_queue = nullptr;
    }

    // 停止解码器
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        if (ch->decoder) {
            gst_decoder_stop(ch->decoder);
            gst_decoder_destroy(ch->decoder);
            ch->decoder = nullptr;
        }
    }

    // 清空显示缓存
    for (int i = 0; i < g_cfg.channel_count; i++) {
        g_cfg.channels[i].set_frame(nullptr);
    }

    // 释放 Worker 上下文
    for (int i = 0; i < WORKER_NUM; i++) {
        yolov5_release_worker(&g_worker_ctxs[i]);
    }
    yolov5_release_base(&g_base_ctx);

    // 释放队列管理器
    if (g_queue_manager) {
        delete g_queue_manager;
        g_queue_manager = nullptr;
    }

    // 帧池自动释放（unique_ptr）
    g_frame_pools.clear();

    free(canvas);
    gst_display_deinit();
    
    printf("[Main] Cleanup done, exiting.\n");
    return 0;
}