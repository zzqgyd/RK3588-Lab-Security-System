/**
 * @file main_process/gst_decoder.cpp
 * @brief GStreamer 多路RTSP解码器 — 支持断流自动重连
 *
 * 设计重点：
 *   1. **健壮的重连机制**：通过总线监听 `ERROR`/`EOS` 消息自动触发重建，保证长期运行。
 *   2. **DMA-BUF 零拷贝优先**：在视频内存共享场景下优先使用，失败时回退到 `memcpy`。
 *   3. **线程安全的帧分发**：使用帧池和环形缓冲区，解耦解码线程与后续处理线程。
 *   4. **资源生命周期管理**：重连时严格先释放再重建，避免内存泄漏和状态混乱。
 */

// ================================================================
// 头文件包含
// ================================================================
#include <gst/gst.h>                // GStreamer 核心库
#include <gst/app/gstappsink.h>    // AppSink 接口，用于将数据拉入应用
#include <gst/allocators/gstdmabuf.h> // DMA-BUF 内存分配器，用于零拷贝
#include <stdio.h>                 // 标准 I/O
#include <sys/mman.h>              // mmap/munmap，映射 DMA-BUF 到用户空间
#include <string.h>                // 字符串操作
#include <stdlib.h>                // 标准库函数
#include <unistd.h>                // sleep/usleep，重连等待

#include "gst_decoder.h"           // 本模块头文件
#include "frame_pool.hpp"          // 帧池，管理 Frame 对象的分配与回收
#include "config.h"                // 全局配置，包含通道参数
#include "queue_manager.hpp"      // 队列管理器，存储环形缓冲区供消费者读取
#include "common.h"                // 公共定义 (FramePtr 等)
#include "recorder_queue.hpp"

// ================================================================
// 全局变量
// ================================================================
extern QueueManager* g_queue_manager; // 全局队列管理器实例，用于获取各通道 RingBuffer
static uint64_t g_stream_seq[MAX_CHANNEL] = {0}; // 每个通道的帧序列号，递增分配
extern AppConfig g_cfg;             // 全局应用配置
extern RecorderPool* g_recorder_pool;
// ================================================================
// 解码器实例结构体 —— 封装单个解码通道的所有状态
// ================================================================
struct GstDecoder {
    GstElement*      pipeline;          // 管道实例
    GstElement*      appsink;           // 数据出口元素，提取原始帧
    GstImageCallback callback;          // 老版本的用户回调（已废弃，保留兼容）
    void*            user_data;         // 回调用户数据（实际复用为 stream_id）

    // ---------- 断流重连支持 ----------
    char             rtsp_url[512];     // 重建所需的 RTSP 地址
    int              stream_id;         // 流的逻辑 ID，对应通道索引
    bool             reconnecting;      // 是否正在执行重连流程 (用于状态保护)
    GstBus*          bus;               // 管道总线，接收系统消息
    guint            bus_watch_id;      // 总线监听器的 ID，用于移除监听
};

// ================================================================
// 前向声明
// ================================================================
static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer user_data);
static GstElement* create_pipeline(const gchar* rtsp_url);

/**
 * @brief 回退模式：当 DMA-BUF 不可用时，通过内存拷贝获取帧数据
 * @param frame 输出的帧对象，会设置其内存指针和属性
 * @param buf   GStreamer 缓冲区，包含原始数据
 * @param map   已映射好的 GstMapInfo，提供数据指针和大小
 * @return 成功返回 true，失败返回 false
 */
static bool fallback_copy(FramePtr frame, GstBuffer* buf, GstMapInfo* map)
{
    frame->is_dmabuf = false;                       // 标记为普通内存帧
    frame->img.virt_addr = (unsigned char*)malloc(map->size); // 分配拷贝内存
    if (!frame->img.virt_addr) return false;        // 分配失败
    memcpy(frame->img.virt_addr, map->data, map->size); // 执行数据拷贝
    frame->img.fd = -1;                             // 无文件描述符
    return true;
}

/**
 * @brief appsink 的 "new-sample" 信号回调 —— 每当新帧就绪时触发
 * 
 * 这是数据流的核心入口。负责从管道拉取帧、解析格式、分配帧内存并推入环形缓冲区。
 * **重连安全性**：重连期间旧管道为 NULL，此回调物理上不会被触发。
 */
static GstFlowReturn new_sample_cb(GstElement* sink, gpointer user_data)
{
    GstDecoder* decoder = (GstDecoder*)user_data;   // 取解码器上下文
    gint width = 0, height = 0;                     // 图像宽高
    const gchar* format;                            // 像素格式字符串
    GstSample* sample = NULL;                       // 从 appsink 提取的样本
    GstBuffer* buf = NULL;                          // 样本中的缓冲区
    GstCaps* caps = NULL;                           // 样本的媒体能力
    GstStructure* str = NULL;                       // 能力结构体

    // user_data 曾被复用存储 int stream_id，此处转换
    int stream_id = (int)(intptr_t)decoder->user_data;
    ChannelContext* ch = &g_cfg.channels[stream_id]; // 获取该通道配置

    // ---- 1. 从 appsink 拉取一帧样本 (阻塞，直到有新帧) ----
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) return GST_FLOW_ERROR;              // 拉取失败，可能流结束

    // ---- 2. 从样本中提取缓冲区 (包含实际二进制数据) ----
    buf = gst_sample_get_buffer(sample);
    if (!buf) {                                      // 无数据
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // ---- 3. 获取媒体能力并解析视频宽度、高度、格式 ----
    caps = gst_sample_get_caps(sample);
    if (!caps) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    str = gst_caps_get_structure(caps, 0);            // 取第一个结构
    if (!str ||
        !gst_structure_get_int(str, "width", &width) || // 解析宽高
        !gst_structure_get_int(str, "height", &height)) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    format = gst_structure_get_string(str, "format"); // 如 "NV12"

    // 缓冲区大小，若未获取到则估算为 NV12 的 1.5 倍像素数
    size_t buf_size = gst_buffer_get_size(buf);
    if (buf_size == 0) buf_size = width * height * 3 / 2;

    // ---- 4. 从帧池获取一个帧对象 (避免频繁 malloc/free) ----
    FramePtr frame;
    if (ch->frame_pool) {
        frame = ch->frame_pool->acquire();            // 从池中借用
    }
    if (!frame) {                                     // 池耗尽或未初始化
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // ---- 5. 填充帧的元信息 ----
    frame->stream_id       = stream_id;
    frame->seq             = g_stream_seq[stream_id]++; // 自增序列号
    frame->pts             = GST_BUFFER_PTS(buf);       // 原始显示时间戳
    frame->img.width       = width;
    frame->img.height      = height;
    frame->img.width_stride  = width;                // 紧密排列，无额外跨距
    frame->img.height_stride = height;
    frame->img.format      = IMAGE_FORMAT_YUV420SP_NV12; // 当前硬编码为 NV12
    frame->img.size        = buf_size;

    // ---- 6. 内存模式选择：优先 DMA-BUF 实现零拷贝，否则拷贝 ----
    GstMemory* mem = gst_buffer_peek_memory(buf, 0);  // 查看第一个内存块
    bool use_dmabuf = gst_is_dmabuf_memory(mem);      // 检查是否为DMA内存

    if (use_dmabuf) {
        // --- DMA-BUF 路径 (零拷贝，内存共享) ---
        int dmabuf_fd = gst_dmabuf_memory_get_fd(mem); // 获取文件描述符
        // 将 DMA 缓冲区映射到进程的虚拟地址空间，以便 CPU 访问
        // void* virt = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
        //                   MAP_SHARED, dmabuf_fd, 0);

        frame->is_dmabuf   = true;                // 标记为 DMA 内存
        frame->gst_sample  = sample;              // 保持引用，防止数据被回收
        frame->img.fd      = dmabuf_fd;           // 存储 fd 用于后续释放
        // frame->img.virt_addr = (unsigned char*)virt; // 映射的虚拟地址
        frame->img.virt_addr = nullptr;
        // 注意：此时未 unref sample，它在帧释放时处理
    } else {
        // --- 回退路径 (传统内存拷贝) ---
        GstMapInfo map;
        // 映射 GstBuffer 到可读内存
        if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
            gst_sample_unref(sample);             // 映射失败，释放资源
            return GST_FLOW_ERROR;
        }
        // 分配并拷贝数据到帧中
        if (!fallback_copy(frame, buf, &map)) {
            gst_buffer_unmap(buf, &map);          // 拷贝失败，释放映射
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }
        gst_buffer_unmap(buf, &map);              // 解除映射
        gst_sample_unref(sample);                 // 释放样本 (数据已拷贝)
    }

    // ---- 7. 将处理好的帧推入环形缓冲区，供消费者线程使用 ----
    if (g_queue_manager) {
        RingBuffer<FramePtr, RING_SIZE>& rb = g_queue_manager->get_queue(stream_id);
        rb.write(std::move(frame));       // 写入 
    }

    return GST_FLOW_OK;
}

// ================================================================
// ★ 工厂函数：根据 RTSP URL 构建并返回新的 GStreamer 管道
//    这是重连机制的基础，实现管道的干净重建。
// ================================================================
static GstElement* create_pipeline(const gchar* rtsp_url)
{
    GError* error = NULL;
    // 管道描述：
    // rtspsrc: 从RTSP获取流，latency=0低延迟
    // -> rtph264depay: 解包RTP H.264
    // -> h264parse: 解析为字节流
    // -> mppvideodec: Rockchip硬件解码器，输出NV12
    // -> queue: 缓冲4帧，若下游慢则丢弃旧帧(leaky)
    // -> appsink: 应用出口，异步，仅保留1帧，新帧到达时丢弃旧帧(drop)
    gchar* pipeline_str = g_strdup_printf(
        "rtspsrc location=%s latency=0 timeout=5000000 protocols=tcp ! "      // 1. RTSP源，低延迟 5 秒收不到数据就触发 EOS/ERROR。
        "rtph264depay ! "                       // 2. 解包RTP H.264去掉RTP头，提取H.264
        "h264parse ! "                          // 3. 解析H.264NAL单元
        "mppvideodec format=NV12 fast-mode=true arm-afbc=false ! " // 4. 硬解码成NV12
        "queue max-size-buffers=4 leaky=downstream ! " // 5. 缓冲4帧，满则丢旧帧
        "appsink name=appsink sync=false max-buffers=1 drop=true", // 6. 应用出口
        rtsp_url);

    GstElement* pipeline = gst_parse_launch(pipeline_str, &error); // 构建管道
    g_free(pipeline_str);                              // 释放描述字符串

    if (!pipeline) {                                   // 构建失败
        g_printerr("[Decoder] Failed to create pipeline: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }
    return pipeline;
}

// ================================================================
// ★ 总线消息处理回调 —— 实现断流重连的核心逻辑
// 
// 监听 ERROR 与 EOS 消息，执行完整的“停止-释放-重建-启动”流程。
// 返回 TRUE 保持监听注册，返回 FALSE 移除监听。
// ================================================================
static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer user_data)
{
    GstDecoder* decoder = (GstDecoder*)user_data;

    switch (GST_MESSAGE_TYPE(msg)) {

    case GST_MESSAGE_ERROR: {                           // 严重错误 (如断流)
        // 防止重入
        if (decoder->reconnecting) {
            g_print("[Decoder] stream %d already reconnecting, skip\n", decoder->stream_id);
            break;
        }
        
        // ===== 新增：断流时停止该路录像 =====
        if (g_recorder_pool) {
            time_t t = time(NULL);
            char end_time[64];
            strftime(end_time, sizeof(end_time), "%Y%m%d_%H%M%S", localtime(&t));
            RecorderTask task = RecorderTask::make_stop(decoder->stream_id, -1, end_time);
            g_recorder_pool->submit(std::move(task));
        }
    

        GError* err = NULL;
        gchar* debug = NULL;
        gst_message_parse_error(msg, &err, &debug);     // 提取错误信息
        g_printerr("[Decoder] stream %d error: %s\n", decoder->stream_id, err->message);
        g_printerr("[Decoder] stream %d disconnected, reconnecting in 3s...\n", decoder->stream_id);
        g_error_free(err);
        g_free(debug);

        // ---- 重连序列 ----
        decoder->reconnecting = true;                   // 1. 标记重连状态
        gst_element_set_state(decoder->pipeline, GST_STATE_NULL); // 2. 停止管道
        usleep(500000 + decoder->stream_id * 100000);;                                 // 3. 等待 0.5 秒
        
        gst_object_unref(decoder->pipeline);            // 4. 销毁旧管道
        decoder->pipeline = NULL;                       //    清空指针
        decoder->appsink  = NULL;                       //    appsink 随管道失效

        GstElement* new_pipeline = create_pipeline(decoder->rtsp_url); // 5. 构建新管道
        if (!new_pipeline) {
            g_printerr("[Decoder] stream %d reconnect failed\n", decoder->stream_id);
            decoder->reconnecting = false;
            decoder->bus_watch_id = 0;
            // 此时总线已失效，无法继续监听，返回FALSE移除此watch
            return FALSE;   //因为管道已经不存在了，监听也没意义
        }

        decoder->pipeline = new_pipeline;               // 6. 更新管道指针

        //这个函数会增加元素的引用计数，所以最后要在 gst_decoder_destroy() 中 unref。
        decoder->appsink = gst_bin_get_by_name(GST_BIN(new_pipeline), "appsink"); // 7. 获取 appsink
        if (!decoder->appsink) {
            g_printerr("[Decoder] stream %d failed to get appsink\n", decoder->stream_id);
            gst_object_unref(new_pipeline);
            decoder->pipeline = NULL;
            decoder->reconnecting = false;
            return FALSE;
        }
        //"emit-signals" - appsink 的一个布尔属性
        g_object_set(decoder->appsink, "emit-signals", TRUE, NULL); // 8. 重新绑定信号
        g_signal_connect(G_OBJECT(decoder->appsink), "new-sample",
                         G_CALLBACK(new_sample_cb), decoder);

        decoder->bus = gst_pipeline_get_bus(GST_PIPELINE(new_pipeline)); // 9. 注册新的总线监听
        decoder->bus_watch_id = gst_bus_add_watch(decoder->bus, on_bus_message, decoder);
        gst_object_unref(decoder->bus);                // 释放引用，watch仍生效

        GstStateChangeReturn ret = gst_element_set_state(new_pipeline, GST_STATE_PLAYING); // 10. 启动播放
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("[Decoder] stream %d failed to start\n", decoder->stream_id);
            decoder->reconnecting = false;
            return FALSE;
        }

        decoder->reconnecting = false;                  // 11. 重连完毕
        g_print("[Decoder] stream %d reconnected\n", decoder->stream_id);
        break;
    }

    case GST_MESSAGE_EOS: {                              // 流结束 (逻辑同 ERROR)
        // 防止重入
        if (decoder->reconnecting) {
            g_print("[Decoder] stream %d already reconnecting, skip\n", decoder->stream_id);
            break;
        }
        g_printerr("[Decoder] stream %d EOS, reconnecting in 3s...\n", decoder->stream_id);

        // ===== 新增：断流时停止该路录像 =====
        if (g_recorder_pool) {
            time_t t = time(NULL);
            char end_time[64];
            strftime(end_time, sizeof(end_time), "%Y%m%d_%H%M%S", localtime(&t));
            RecorderTask task = RecorderTask::make_stop(decoder->stream_id, -1, end_time);
            g_recorder_pool->submit(std::move(task));
        }
    

        decoder->reconnecting = true;
        gst_element_set_state(decoder->pipeline, GST_STATE_NULL);
        usleep(500000 + decoder->stream_id * 100000);;
        gst_object_unref(decoder->pipeline);
        decoder->pipeline = NULL;
        decoder->appsink  = NULL;

        GstElement* new_pipeline = create_pipeline(decoder->rtsp_url);
        if (!new_pipeline) {
            g_printerr("[Decoder] stream %d reconnect failed\n", decoder->stream_id);
            decoder->reconnecting = false;
            decoder->bus_watch_id = 0;
            return FALSE;
        }
        decoder->pipeline = new_pipeline;
        decoder->appsink = gst_bin_get_by_name(GST_BIN(new_pipeline), "appsink");
        if (!decoder->appsink) {
            gst_object_unref(new_pipeline);
            decoder->pipeline = NULL;
            decoder->reconnecting = false;
            return FALSE;
        }

        g_object_set(decoder->appsink, "emit-signals", TRUE, NULL);
        g_signal_connect(G_OBJECT(decoder->appsink), "new-sample",
                         G_CALLBACK(new_sample_cb), decoder);

        decoder->bus = gst_pipeline_get_bus(GST_PIPELINE(new_pipeline));
        decoder->bus_watch_id = gst_bus_add_watch(decoder->bus, on_bus_message, decoder);
        gst_object_unref(decoder->bus);

        gst_element_set_state(new_pipeline, GST_STATE_PLAYING);
        decoder->reconnecting = false;
        g_print("[Decoder] stream %d reconnected after EOS\n", decoder->stream_id);
        break;
    }

    default:                                           // 其它消息忽略
        break;
    }

    return TRUE;                                       // 保持监听器活跃
}

// ================================================================
// 公共接口：创建解码器实例
// 参数 callback 和 user_data 保留兼容性，user_data 实际用于传递 stream_id
// ================================================================
GstDecoder* gst_decoder_create(const gchar* rtsp_url, GstImageCallback callback,
                                void* user_data)
{
    GstDecoder* decoder = g_new0(GstDecoder, 1);       // 分配并零初始化

    decoder->callback   = callback;                    // 保存回调（暂未使用）
    decoder->user_data  = user_data;                   // 保存用户数据，即 stream_id
    decoder->stream_id  = (int)(intptr_t)user_data;    // 双重保存以便直接使用
    decoder->reconnecting = false;                     // 初始未重连

    strncpy(decoder->rtsp_url, rtsp_url, sizeof(decoder->rtsp_url) - 1); // 保存 URL
    decoder->rtsp_url[sizeof(decoder->rtsp_url) - 1] = '\0';

    // 创建初始管道
    decoder->pipeline = create_pipeline(rtsp_url);
    if (!decoder->pipeline) {
        g_free(decoder);
        return NULL;
    }

    // 获取并配置 appsink
    decoder->appsink = gst_bin_get_by_name(GST_BIN(decoder->pipeline), "appsink");
    if (!decoder->appsink) {
        gst_object_unref(decoder->pipeline);
        g_free(decoder);
        return NULL;
    }
    g_object_set(decoder->appsink, "emit-signals", TRUE, NULL);
    g_signal_connect(G_OBJECT(decoder->appsink), "new-sample",
                     G_CALLBACK(new_sample_cb), decoder);

    // 注册总线消息监听，用于断流重连
    decoder->bus = gst_pipeline_get_bus(GST_PIPELINE(decoder->pipeline));
    //gst_bus_add_watch() 内部会增加总线的引用计数
    decoder->bus_watch_id = gst_bus_add_watch(decoder->bus, on_bus_message, decoder);
    gst_object_unref(decoder->bus);// 释放我们自己的引用，watch 会保持引用

    g_print("[Decoder] Created stream %d: %s\n", decoder->stream_id, rtsp_url);
    return decoder;
}

// ================================================================
// 公共接口：启动管道播放
// ================================================================
int gst_decoder_start(GstDecoder* dec)
{
    if (!dec || !dec->pipeline) return -1;
    GstStateChangeReturn ret = gst_element_set_state(dec->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print("[Decoder] stream %d start failed\n", dec->stream_id);
        return -1;
    }
    g_print("[Decoder] stream %d pipeline started\n", dec->stream_id);
    return 0;
}

// ================================================================
// 公共接口：停止管道播放并移除总线监听
// ================================================================
int gst_decoder_stop(GstDecoder* dec)
{
    if (!dec || !dec->pipeline) return -1;

    if (dec->bus_watch_id > 0) {
        g_source_remove(dec->bus_watch_id);            // 移除总线监听
        dec->bus_watch_id = 0;
    }

    gst_element_set_state(dec->pipeline, GST_STATE_NULL); // 停止管道
    g_print("[Decoder] stream %d pipeline stopped\n", dec->stream_id);
    return 0;
}

// ================================================================
// 公共接口：彻底销毁解码器实例，释放所有关联资源
// ================================================================
void gst_decoder_destroy(GstDecoder* dec)
{
    if (!dec) return;

    gst_decoder_stop(dec);                             // 先停止
    //因为gst_bin_get_by_name() 增加了 appsink 的引用计数
    if (dec->appsink) {                                // 释放 appsink 引用
        gst_object_unref(dec->appsink);
        dec->appsink = NULL;
    }
    if (dec->pipeline) {                               // 释放管道引用
        gst_object_unref(dec->pipeline);
        dec->pipeline = NULL;
    }

    g_free(dec);                                       // 释放解码器结构体本身
    g_print("[Decoder] instance destroyed\n");
}