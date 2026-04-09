/**
 * @file demo.cpp
 * @brief 多路RTSP视频流 + YOLO推理 + 2x2显示主程序
 * 
 * 架构说明：
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  Stream 0: GstDecoder ──→ Queue[0] ──┐                          │
 * │  Stream 1: GstDecoder ──→ Queue[1] ──┤                          │
 * │  Stream 2: GstDecoder ──→ Queue[2] ──┼──→ InferThread           │
 * │  Stream 3: GstDecoder ──→ Queue[3] ──┘    (单线程轮询推理)       │
 * │                                                                 │
 * │  同时：每路的最新帧缓存到 ChannelContext，用于 2x2 显示合成       │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * 线程模型：
 *   - 主线程：GStreamer 消息循环 + 2x2 显示合成
 *   - 每路解码器：GStreamer streaming 线程（回调中入队）
 *   - 推理线程：单线程轮询所有队列，执行 YOLO 推理
 */
#include <signal.h>
#include <gst/gst.h>
#include <unistd.h>
#include <iostream>
#include <cstdio>
// GStreamer 模块
#include "gst_decoder.h"
#include "gst_display.h"

// 推理模块
#include "yolov5_infer.h"
#include "image_drawing.h"
#include "common.h"

// 队列模块（C++）
#include "queue_manager.hpp"
#include "infer_thread.hpp"

// 配置模块
#include "config.h"

// ============================================================================
// 宏定义
// ============================================================================
#define MAX_CHANNEL        4       ///< 最大支持流路数
#define CONFIG_LINE_MAX    256     ///< 配置文件行最大长度
#define CANVAS_W           1280    ///< 显示画布宽度
#define CANVAS_H           960     ///< 显示画布高度

// ============================================================================
// 全局变量
// ============================================================================
static volatile bool g_running = true;    ///< 程序运行标志（Ctrl+C 置 false）

// ===== 全局 C++ 对象指针 =====
// 由于 demo.c 可能是纯 C，这里统一用 void* 存储，实际使用时强转
// 如果确认为 C++ 编译，可以直接用具体类型
QueueManager* g_queue_manager = nullptr;       ///< 队列管理器
InferThread* g_infer_thread = nullptr;        ///< 推理线程
rknn_app_context_t* g_yolo_ctx_array = nullptr;  ///< YOLO上下文数组

AppConfig g_cfg = {0};

void sigint_handler(int sig) {
    g_running = false;
    printf("\n退出...\n");
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
        // 去除末尾换行符
        line[strcspn(line, "\n")] = 0;
        
        // 解析模型路径
        if (strstr(line, "path =")) {
            sscanf(line, "path = %s", g_cfg.model_path);
        }
        
        // 解析RTSP地址（每出现一次增加一个通道）
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

/**
 * @brief 2x2 软件合成（将多路画面拼接到一个画布）
 * @param canvas   目标画布内存
 * @param channels 通道数组
 * @param count    通道数量
 */
void composite_2x2_rga(uint8_t *canvas, ChannelContext *channels, int count)
{
    memset(canvas, 0, CANVAS_W * CANVAS_H * 3);

    const int cell_cols = 2;
    const int cell_rows = 2;
    const int cell_w = CANVAS_W / cell_cols;
    const int cell_h = CANVAS_H / cell_rows;

    for (int i = 0; i < count && i < MAX_CHANNEL; i++) {
        ChannelContext *ch = &channels[i];
        g_mutex_lock(&ch->frame_lock);

        image_buffer_t *img = &ch->frame_buf;
        if (!img->virt_addr || img->width <= 0 || img->height <= 0) {
            g_mutex_unlock(&ch->frame_lock);
            continue;   // 还没有收到第一帧
        }

        // 计算缩放比例（保持宽高比，居中显示）
        float scale_w = (float)cell_w / img->width;
        float scale_h = (float)cell_h / img->height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        int draw_w = img->width * scale;
        int draw_h = img->height * scale;
        int ox = (cell_w - draw_w) / 2;
        int oy = (cell_h - draw_h) / 2;

        int dst_x = (i % 2) * cell_w + ox;
        int dst_y = (i / 2) * cell_h + oy;

        // 软合成(后面换RGA!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!)
        for (int y = 0; y < draw_h; y++) {
            int sy = (int)(y / scale);
            uint8_t* src_row = img->virt_addr + sy * img->width * 3;
            uint8_t* dst_row = canvas + (dst_y + y) * CANVAS_W * 3 + dst_x * 3;
            
            for (int x = 0; x < draw_w; x++) {
                int sx = (int)(x / scale);
                dst_row[x*3 + 0] = src_row[sx*3 + 0];
                dst_row[x*3 + 1] = src_row[sx*3 + 1];
                dst_row[x*3 + 2] = src_row[sx*3 + 2];
            }
        }
        g_mutex_unlock(&ch->frame_lock);
    }
}

// ==============================
// 主函数
// ==============================
int main(int argc, char *argv[]) {
    // ===== 1. 初始化 =====
    signal(SIGINT, sigint_handler);
    gst_init(NULL, NULL);
    if (load_config("./config/config.ini") < 0) return -1;

    // ===== 2. 创建队列管理器 =====
    g_queue_manager = new QueueManager();
    printf("[Main] QueueManager created\n");

    // ===== 3. 初始化每路的 YOLO 上下文 =====
    g_yolo_ctx_array = new rknn_app_context_t[MAX_CHANNEL];
    for (int i = 0; i < g_cfg.channel_count; i++) {
        int ret = yolov5_init(&g_yolo_ctx_array[i], g_cfg.model_path);
        if (ret < 0) {
            printf("[Main] Failed to init YOLO for stream %d\n", i);
            return -1;
        }
        printf("[Main] YOLO initialized for stream %d\n", i);
    }

    // ===== 4. 创建并启动推理线程 =====
    g_infer_thread = new InferThread(g_queue_manager, g_cfg.channel_count, g_yolo_ctx_array);
    g_infer_thread->start();
    printf("[Main] InferThread started\n");

    // ===== 5. 初始化每路的显示缓存 =====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        
        // 分配显示缓存（最大1920x1080 RGB，约6MB）
        ch->frame_buf.virt_addr = (uint8_t*)malloc(1920 * 1080 * 3);
        ch->frame_buf.size = 1920 * 1080 * 3;
        ch->frame_buf.width = 0;
        ch->frame_buf.height = 0;
        ch->frame_buf.format = IMAGE_FORMAT_RGB888;
        
        g_mutex_init(&ch->frame_lock);
    }

    // ===== 6. 初始化显示模块 =====
    uint8_t *canvas = (uint8_t *)malloc(CANVAS_W * CANVAS_H * 3);
    if (!canvas) {
        printf("[Main] Failed to allocate canvas\n");
        return -1;
    }
    if (gst_display_init(CANVAS_W, CANVAS_H) < 0) {
        printf("[Main] Failed to init display\n");
        free(canvas);
        return -1;
    }
    printf("[Main] Display initialized (%dx%d)\n", CANVAS_W, CANVAS_H);

    // ===== 7. 创建并启动解码器（在显示初始化后，避免显示未就绪）=====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        
        // 创建解码器，user_data 传入 stream_id
        ch->decoder = gst_decoder_create(ch->rtsp, NULL, (void*)(intptr_t)i);
        if (!ch->decoder) {
            printf("[Main] Failed to create decoder for stream %d\n", i);
            return -1;
        }
        gst_decoder_start(ch->decoder);
        printf("[Main] Decoder started for stream %d: %s\n", i, ch->rtsp);
    }

    // ===== 8. 主循环 =====
    printf("\n[Main] === System running, press Ctrl+C to exit ===\n\n");
    while (g_running) {
        // 驱动 GStreamer 消息循环（必须！否则 pipeline 不工作）
        g_main_context_iteration(NULL, FALSE);
        // 合成2x2画面并推送到显示
        composite_2x2_rga(canvas, g_cfg.channels, g_cfg.channel_count);
        gst_display_push_rgb(CANVAS_W, CANVAS_H, canvas, CANVAS_W * CANVAS_H * 3);
        usleep(16000);  // 约 60fps (1000000/60 ≈ 16666)
    }

    // ===== 9. 清理资源 =====
    printf("\n[Main] Shutting down...\n");
    // 9.1 停止并销毁解码器
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        if (ch->decoder) {
            gst_decoder_stop(ch->decoder);
            gst_decoder_destroy(ch->decoder);
            ch->decoder = NULL;
        }
    }
    printf("[Main] Decoders destroyed\n");

    // 9.2 停止推理线程
    if (g_infer_thread) {
        g_infer_thread->stop();
        delete g_infer_thread;
        g_infer_thread = nullptr;
        printf("[Main] InferThread stopped\n");
    }

    // 9.3 释放显示缓存
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        g_mutex_lock(&ch->frame_lock);
        if (ch->frame_buf.virt_addr) {
            free(ch->frame_buf.virt_addr);
            ch->frame_buf.virt_addr = NULL;
        }
        g_mutex_unlock(&ch->frame_lock);
        g_mutex_clear(&ch->frame_lock);
    }

    // 9.4 释放 YOLO 上下文
    if (g_yolo_ctx_array) {
        for (int i = 0; i < g_cfg.channel_count; i++) {
            yolov5_release(&g_yolo_ctx_array[i]);
        }
        delete[] g_yolo_ctx_array;
        g_yolo_ctx_array = nullptr;
        printf("[Main] YOLO contexts released\n");
    }

    // 9.5 释放队列管理器
    if (g_queue_manager) {
        delete g_queue_manager;
        g_queue_manager = nullptr;
        printf("[Main] QueueManager destroyed\n");
    }

    // 9.6 释放显示资源
    free(canvas);
    gst_display_deinit();
    printf("[Main] Display deinitialized\n");
    
    printf("[Main] Cleanup done, exiting.\n");
    return 0;
}