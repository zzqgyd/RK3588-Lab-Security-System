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

// RGA 头文件
#include "im2d.h"
#include "RgaUtils.h"

// 队列模块（C++）
#include "queue_manager.hpp"
#include "mpmc_queue.hpp"
#include "scheduler.hpp"
#include "worker_pool.hpp"

// 配置模块
#include "config.h"

// ============================================================================
// 宏定义
// ============================================================================
#define MAX_CHANNEL        4       ///< 最大支持流路数
#define CONFIG_LINE_MAX    256     ///< 配置文件行最大长度
#define CANVAS_W           3840    ///< 显示画布宽度
#define CANVAS_H           2160     ///< 显示画布高度
#define WORKER_NUM         3       ///< Worker 数量

// ============================================================================
// 全局变量
// ============================================================================
static volatile bool g_running = true;    ///< 程序运行标志（Ctrl+C 置 false）

// 队列管理器
QueueManager* g_queue_manager = nullptr;

MPMCQueue<Frame*>* g_inference_queue = nullptr;
Scheduler* g_scheduler = nullptr;
WorkerPool* g_worker_pool = nullptr;

// ===== 全局 C++ 对象指针 =====
// ===== 模型相关全局变量（第一阶段新增）=====
base_model_context_t g_base_ctx = {0};              // 基础模型（全局唯一）
worker_context_t g_worker_ctxs[WORKER_NUM] = {{0}}; // Worker 上下文数组（3个）

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
 * @brief 2x2 NV12 格式 RGA 硬件合成
 * 
 * 使用 Rockchip RGA 硬件模块进行缩放和放置，CPU 零开销。
 * RGA 自动处理 NV12 格式的 Y 和 UV 平面，无需手动分离。
 * 
 * @param canvas   目标画布内存（NV12 格式，大小 = CANVAS_W * CANVAS_H * 3/2）
 * @param channels 通道数组
 * @param count    通道数量
 */
void composite_2x2_rga_nv12(uint8_t *canvas, ChannelContext *channels, int count)
{
    const int cell_cols = 2;
    const int cell_rows = 2;
    const int cell_w = CANVAS_W / cell_cols;
    const int cell_h = CANVAS_H / cell_rows;

    // ===== 1. 清空画布为黑色（NV12 格式）=====
    // Y 平面：全部填 0（黑色亮度）
    memset(canvas, 0, CANVAS_W * CANVAS_H);
    // UV 平面：全部填 128（黑色色度，NV12 格式 U=V=128 表示无色）
    memset(canvas + CANVAS_W * CANVAS_H, 128, CANVAS_W * CANVAS_H / 2);

    // ===== 2. 创建画布的 RGA buffer（只需创建一次）=====
    rga_buffer_t dst = wrapbuffer_virtualaddr(canvas, CANVAS_W, CANVAS_H, RK_FORMAT_YCbCr_420_SP);
    
    // 空的 pat buffer（不使用）
    rga_buffer_t pat = {0};
    
    for (int i = 0; i < count && i < MAX_CHANNEL; i++)
    {
        ChannelContext* ch = &channels[i];
        g_mutex_lock(&ch->frame_lock);
        image_buffer_t *src_buf = &ch->frame_buf;
        if(!src_buf->virt_addr || src_buf->width <= 0 || src_buf->height <= 0){
            g_mutex_unlock(&ch->frame_lock);
            continue; // 跳过无效帧
        }

        // ===== 3. 创建源图像的 RGA buffer =====
        rga_buffer_t src = wrapbuffer_virtualaddr(
            src_buf->virt_addr,
            src_buf->width,
            src_buf->height,
            RK_FORMAT_YCbCr_420_SP  // NV12
        );

        // ===== 4. 计算缩放和位置（保持宽高比，居中显示）=====
        float scale_w = (float)cell_w / src_buf->width;
        float scale_h = (float)cell_h / src_buf->height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        
        // NV12 格式要求宽高为偶数，强制对齐
        int draw_w = ((int)(src_buf->width * scale)) & ~1;
        int draw_h = ((int)(src_buf->height * scale)) & ~1;
        
        // 居中偏移（也保持偶数对齐）
        int ox = ((cell_w - draw_w) / 2) & ~1;
        int oy = ((cell_h - draw_h) / 2) & ~1;
        
        // 目标区域（在 2x2 网格中的位置 + 居中偏移）
        im_rect dst_rect;
        dst_rect.x = (i % 2) * cell_w + ox;
        dst_rect.y = (i / 2) * cell_h + oy;
        dst_rect.width = draw_w;
        dst_rect.height = draw_h;

        // 源区域（整帧）
        im_rect src_rect;
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = src_buf->width;
        src_rect.height = src_buf->height;

        im_rect prect = {0, 0, 0, 0};  // pat 区域（不使用）

        // ===== 5. RGA 硬件执行缩放+拷贝 =====
        // 注意：usage=0 表示只做缩放，不旋转/翻转
        IM_STATUS ret = improcess(
            src,      // 源
            dst,      // 目标
            pat,      // pat（不使用）
            src_rect, // 源区域
            dst_rect, // 目标区域
            prect,    // pat区域（不使用）
            0         // usage
        );
        
        if (ret != IM_STATUS_SUCCESS) {
            printf("[RGA] Stream %d: imcrop failed: %s\n", 
                   i, imStrError(ret));
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

    // 步骤 3.1：初始化基础模型（只调用一次，权重加载到 NPU）
    int ret = yolov5_init_base(&g_base_ctx, g_cfg.model_path);
    if (ret < 0) {
        printf("[Main] Failed to init base model\n");
        delete g_queue_manager;
        return -1;
    }
    printf("[Main] Base model initialized\n");
    
    // 步骤 3.2：复制 3 个 Worker 上下文（后续给 3 个 Worker 线程使用）
    for (int i = 0; i < WORKER_NUM; i++) {
        ret = yolov5_dup_worker_context(&g_base_ctx, &g_worker_ctxs[i]);
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


    // ===== 4. 创建 MPMC 队列（容量 16）=====
    g_inference_queue = new MPMCQueue<Frame*>(16);
    printf("[Main] MPMC queue created (capacity: 16)\n");

    // ===== 5. 创建并启动调度器 =====
    g_scheduler = new Scheduler(g_queue_manager, g_inference_queue, g_cfg.channel_count);
    g_scheduler->start();
    printf("[Main] Scheduler started\n");

    // ===== 6. 创建并启动 Worker 池 =====
    g_worker_pool = new WorkerPool(
        WORKER_NUM,
        g_inference_queue,
        g_worker_ctxs,
        g_cfg.channels,
        g_cfg.channel_count
    );
    g_worker_pool->start();
    printf("[Main] WorkerPool started (%d workers)\n", WORKER_NUM);

    // ===== 5. 初始化每路的显示缓存 =====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        
        // 先不分配内存，等第一帧到来时根据实际分辨率分配!!!!!!!!!!!!!!!!
        ch->frame_buf.virt_addr = NULL;
        ch->frame_buf.size = 0;
        ch->frame_buf.width = 0;
        ch->frame_buf.height = 0;
        ch->frame_buf.format = IMAGE_FORMAT_YUV420SP_NV12;  // 改为 NV12
        
        g_mutex_init(&ch->frame_lock);
    }

     // ===== 6. 初始化显示模块 =====
    size_t canvas_size = CANVAS_W * CANVAS_H * 3 / 2;
    uint8_t *canvas = (uint8_t *)malloc(canvas_size);
    if (!canvas) {
        printf("[Main] Failed to allocate canvas\n");
        // 清理模型资源
        for (int i = 0; i < WORKER_NUM; i++) {
            yolov5_release_worker(&g_worker_ctxs[i]);
        }
        yolov5_release_base(&g_base_ctx);
        delete g_queue_manager;
        return -1;
    }
    if (gst_display_init(CANVAS_W, CANVAS_H) < 0) {
        printf("[Main] Failed to init display\n");
        free(canvas);
        for (int i = 0; i < WORKER_NUM; i++) {
            yolov5_release_worker(&g_worker_ctxs[i]);
        }
        yolov5_release_base(&g_base_ctx);
        delete g_queue_manager;
        return -1;
    }
    printf("[Main] Display initialized (%dx%d)\n", CANVAS_W, CANVAS_H);

    // ===== 7. 创建并启动解码器（在显示初始化后，避免显示未就绪）=====
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext* ch = &g_cfg.channels[i];
        ch->decoder = gst_decoder_create(ch->rtsp, NULL, (void*)(intptr_t)i);
        if (!ch->decoder) {
            printf("[Main] Failed to create decoder for stream %d\n", i);
            // 清理已创建的解码器
            for (int j = 0; j < i; j++) {
                gst_decoder_stop(g_cfg.channels[j].decoder);
                gst_decoder_destroy(g_cfg.channels[j].decoder);
            }
            gst_display_deinit();
            free(canvas);
            for (int j = 0; j < WORKER_NUM; j++) {
                yolov5_release_worker(&g_worker_ctxs[j]);
            }
            yolov5_release_base(&g_base_ctx);
            delete g_queue_manager;
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
        composite_2x2_rga_nv12(canvas, g_cfg.channels, g_cfg.channel_count);
        gst_display_push_nv12(CANVAS_W, CANVAS_H, canvas, canvas_size);
        //防止 CPU 100% 空转
        //控制显示帧率
        //给其他线程执行机会,休眠时让出 CPU 给 Worker
        usleep(16000);  // 约 60fps (1000000/60 ≈ 16666)    绝对不能去掉！！！！！！！！
    }

    // ===== 9. 清理资源 =====
    printf("\n[Main] Shutting down...\n");
    // 停止 Worker 池（先停消费者）
    if (g_worker_pool) {
        g_worker_pool->stop();
        delete g_worker_pool;
        g_worker_pool = nullptr;
        printf("[Main] WorkerPool destroyed\n");
    }

    // 停止调度器
    if (g_scheduler) {
        g_scheduler->stop();
        delete g_scheduler;
        g_scheduler = nullptr;
        printf("[Main] Scheduler destroyed\n");
    }

    // 释放 MPMC 队列
    if (g_inference_queue) {
        delete g_inference_queue;
        g_inference_queue = nullptr;
        printf("[Main] MPMC queue destroyed\n");
    }
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

    // 9.2 释放显示缓存
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

    // 9.3 释放 Worker 上下文
    for (int i = 0; i < WORKER_NUM; i++) {
        yolov5_release_worker(&g_worker_ctxs[i]);
    }
    printf("[Main] Worker contexts released\n");

    // 9.4 释放基础模型
    yolov5_release_base(&g_base_ctx);
    printf("[Main] Base model released\n");


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