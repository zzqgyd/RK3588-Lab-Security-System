#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gst_decoder.h"

#include "gst_decoder.h"
#include "config.h"           
#include "queue_manager.hpp"   // 队列管理器
#include "common.h"
#include "config.h"

extern QueueManager* g_queue_manager;   ///< 在 demo.cpp 中定义

// ============================================================================
// 解码器实例结构体（对外隐藏,每路一份）
// ============================================================================
struct GstDecoder {
    GstElement*      pipeline;     ///< GStreamer 管道对象
    GstElement*      appsink;      ///< appsink 元素（数据出口）
    GstImageCallback callback;     ///< 用户回调函数（已废弃，不再使用）
    void*            user_data;    ///< 回调用户数据（实际存储 stream_id）
};

/**
 * @brief GStreamer appsink 的 new-sample 信号回调
 * @param sink      appsink 元素
 * @param user_data GstDecoder 实例指针
 * @return GST_FLOW_OK 成功，GST_FLOW_ERROR 失败
 * 
 * 注意：此函数在 GStreamer streaming 线程中执行
 * 职责：取出帧数据 → 封装 Frame → 入队 → 返回
 * 不能做耗时操作！推理已移到 InferThread
 */
static GstFlowReturn new_sample_cb(GstElement *sink, gpointer user_data)
{
    GstDecoder *decoder = (GstDecoder *)user_data;
    gint width=0, height=0;         // 图像宽度和高度
    const gchar *format;        // 图像格式
    GstSample *sample = NULL;   // 采样数据对象
    GstBuffer *buf = NULL;      // 图像缓冲区
    GstCaps *caps = NULL;       // 图像格式信息
    GstStructure *str = NULL;   // 图像结构信息
    GstMapInfo map;             // 内存映射信息

    // 从 user_data 中取出 stream_id
    int stream_id = (int)(intptr_t)decoder->user_data;
    
    // ===== 1. 从 appsink 拉取一帧 Sample =====
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        g_printerr("[Decoder] Failed to pull sample, stream=%d\n", stream_id);
        return GST_FLOW_ERROR;
    }
    
    // ===== 2. 获取 Buffer（包含实际图像数据）=====
    buf = gst_sample_get_buffer(sample);
    if (!buf) {
        g_printerr("[Decoder] Failed to get buffer, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // ===== 3. 获取 Caps（包含图像格式信息）=====
    caps = gst_sample_get_caps(sample);
    if (!caps) {
        g_printerr("[Decoder] Failed to get caps, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // ===== 4. 获取 Structure =====
    str = gst_caps_get_structure(caps, 0);
    if (!str) {
        g_printerr("[Decoder] Failed to get structure, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 5. 获取图像宽度
    if (!gst_structure_get_int(str, "width", &width)) {
        g_print("Error: Failed to get width from structure, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 6. 获取图像高度
    if (!gst_structure_get_int(str, "height", &height)) {
        g_print("Error: Failed to get height from structure, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 7. 获取图像格式
    format = gst_structure_get_string(str, "format");
    if (!format) {
        g_print("Error: Failed to get format from structure, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 8. 映射图像缓冲区到内存!!!!!!!!!!!!!!!!!!!!!!!!!
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        g_print("Error: Failed to map buffer, stream=%d\n", stream_id);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // ===== 9. 创建 Frame 并入队 =====
    Frame* frame = new Frame();
    if (!frame) {
        g_printerr("[Decoder] Failed to allocate Frame, stream=%d\n", stream_id);
        gst_buffer_unmap(buf, &map);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 填充帧信息
    frame->stream_id = stream_id;
    frame->pts = 0;  // TODO: 从 GStreamer 获取真实 PTS!!!!!!!!!!!!!!!!!!!!!!
    
    frame->img.width = width;
    frame->img.height = height;
    frame->img.width_stride = width;
    frame->img.height_stride = height;
    frame->img.format = IMAGE_FORMAT_RGB888;
    frame->img.size = map.size;
    frame->img.fd = -1;  // 暂无 DMA-BUF!!!!!!!!!!!!!!!!!!

    // 分配内存并拷贝（临时方案，后续改为零拷贝/内存池）!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    frame->img.virt_addr = (unsigned char*)malloc(map.size);
    memcpy(frame->img.virt_addr, map.data, map.size);
    if (!frame->img.virt_addr) {
        g_printerr("[Decoder] Failed to malloc image buffer, stream=%d, size=%zu\n", 
                   stream_id, map.size);
        delete frame;
        gst_buffer_unmap(buf, &map);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 入队（传递给推理线程）
    SPSCQueue<Frame*, QUEUE_SIZE>& q = g_queue_manager->get_queue(stream_id);
    if (!q.push(frame)) {
        // 队列满，丢弃此帧（防止内存爆炸）
        free(frame->img.virt_addr);
        delete frame;
    }


    // 10. 解除内存映射
    gst_buffer_unmap(buf, &map);
    
    // 11. 释放采样数据对象
    gst_sample_unref(sample);
    
    // 12. 返回处理成功
    return GST_FLOW_OK;
}

/**
 * @brief 创建解码器实例
 * @param rtsp_url  RTSP 流地址
 * @param callback  图像回调函数
 * @param user_data 回调用户数据（传入 stream_id）
 * @return 成功返回 GstDecoder*，失败返回 NULL
 */
GstDecoder *gst_decoder_create(const gchar *rtsp_url, GstImageCallback callback, void *user_data)
{
    //g_new0 是 GLib 库 提供的一个宏，用于动态分配内存并初始化为零
    GstDecoder *decoder = g_new0(GstDecoder, 1);
    GError *error = NULL;
    gchar *pipeline_str = NULL;

    // 1. 初始化解码器实例
    decoder->callback = callback;      // 保留但不使用
    decoder->user_data = user_data;    // 存储 stream_id

    int stream_id = (int)(intptr_t)user_data;

    // 2. 构建GStreamer管道字符串
    pipeline_str = g_strdup_printf("rtspsrc location=%s latency=0 ! "
                                  "rtph264depay ! "
                                  "h264parse ! "
                                  "mppvideodec format=NV12 fast-mode=true arm-afbc=false ! "
                                  "videoconvert ! video/x-raw,format=RGB ! "
                                  "appsink name=appsink sync=false max-buffers=1 drop=true",
                                  rtsp_url);

    // 3. 创建GStreamer管道
    decoder->pipeline = gst_parse_launch(pipeline_str, &error);
    g_free(pipeline_str);
    if (!decoder->pipeline) {
        g_print("Error: Failed to create pipeline: %s\n", error->message);
        g_error_free(error);
        g_free(decoder);
        return NULL;
    }

    // 4. 获取appsink元素   通过"appsink"得到pipeline处理后得到的数据
    decoder->appsink = gst_bin_get_by_name(GST_BIN(decoder->pipeline), "appsink");
    if (!decoder->appsink) {
        g_print("Error: Failed to get appsink element\n");
        gst_object_unref(decoder->pipeline);
        g_free(decoder);
        return NULL;
    }

    // 5. 设置appsink属性   appsink有输出，通知     参数固定
    g_object_set(decoder->appsink, "emit-signals", TRUE, NULL);

    // 6. 连接新样本信号到回调函数  
    // main-loop循环检测到通知如果和new-sample匹配调用回调函数      "new-sample" 是固定信号名
    //GStreamer 所有元素 默认都在 main loop 里。    所以也不用手动添加
    // 把 decoder 实例传给回调（多路核心）
    g_signal_connect(G_OBJECT(decoder->appsink), "new-sample", 
                     G_CALLBACK(new_sample_cb), decoder);

    // 7.返回解码器实例
    g_print("[Decoder] Created stream %d: %s\n", stream_id, rtsp_url);
    return decoder;
}

/**
 * @brief 启动GStreamer管道
 * @return 启动结果，0表示成功，-1表示失败
 */
int gst_decoder_start(GstDecoder *dec)
{
    if(!dec) return -1;
    // 启动GStreamer管道
    GstStateChangeReturn ret = gst_element_set_state(dec->pipeline, GST_STATE_PLAYING);
    if(ret == GST_STATE_CHANGE_FAILURE) {
        g_print("Error: Failed to start pipeline\n");
        return -1;
    }
    g_print("GStreamer pipeline started\n");
    return 0;
}

/**
 * @brief 停止GStreamer管道
 * @return 停止结果，0表示成功，-1表示失败
 */
int gst_decoder_stop(GstDecoder *dec)
{
    if(!dec) return -1;
    // 停止GStreamer管道
    gst_element_set_state(dec->pipeline, GST_STATE_NULL);
    g_print("GStreamer pipeline stopped\n");
    return 0;
}

/**
 * @brief 释放GStreamer管道资源
 */
void gst_decoder_destroy(GstDecoder *dec)
{
    if(!dec) return;
    
    // 确保管道已停止
    gst_element_set_state(dec->pipeline, GST_STATE_NULL);

    // 释放 appsink 引用
    if (dec->appsink) {
        gst_object_unref(dec->appsink);
        dec->appsink = NULL;
    }
    // 释放管道引用
    if (dec->pipeline) {
        gst_object_unref(dec->pipeline);
        dec->pipeline = NULL;
    }

    // 释放解码器实例
    g_free(dec);
    
    g_print("GStreamer pipeline destroyed\n");
}