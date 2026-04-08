#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include "gst_decoder.h"

// 这些大写宏 = 安全的类型强制转换！
// 为了兼容 GLib 面向对象体系

/* 解码器实例结构体：每路独立一份 */
struct GstDecoder {
    GstElement *pipeline;       // GStreamer管道对象
    GstElement *appsink;        // 应用程序数据输出口
    GstImageCallback callback;  // 图像数据回调函数
    void *user_data;            // 实例指针
};

/**
 * @brief 当GStreamer解码出一帧数据时执行的回调函数
 * @param sink appsink元素指针
 * @param user_data 实例指针
 * @return 处理结果，返回GST_FLOW_OK表示处理成功
 */
static GstFlowReturn new_sample_cb(GstElement *sink, gpointer user_data)
{
    GstDecoder *decoder = (GstDecoder *)user_data;
    gint width, height;         // 图像宽度和高度
    const gchar *format;        // 图像格式
    GstSample *sample = NULL;   // 采样数据对象
    GstBuffer *buf = NULL;      // 图像缓冲区
    GstCaps *caps = NULL;       // 图像格式信息
    GstStructure *str = NULL;   // 图像结构信息
    GstMapInfo map;             // 内存映射信息
    gpointer data = NULL;       // 图像数据指针
    
    // 1. 从appsink中取出一帧数据
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        g_print("Error: Failed to pull sample from appsink\n");
        return GST_FLOW_ERROR;
    }
    
    // 2. 获取图像缓冲区
    buf = gst_sample_get_buffer(sample);
    if (!buf) {
        g_print("Error: Failed to get buffer from sample\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 3. 获取图像格式信息
    caps = gst_sample_get_caps(sample);
    if (!caps) {
        g_print("Error: Failed to get caps from sample\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 4. 获取图像结构信息
    str = gst_caps_get_structure(caps, 0);
    if (!str) {
        g_print("Error: Failed to get structure from caps\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 5. 获取图像宽度
    if (!gst_structure_get_int(str, "width", &width)) {
        g_print("Error: Failed to get width from structure\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 6. 获取图像高度
    if (!gst_structure_get_int(str, "height", &height)) {
        g_print("Error: Failed to get height from structure\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 7. 获取图像格式
    format = gst_structure_get_string(str, "format");
    if (!format) {
        g_print("Error: Failed to get format from structure\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 8. 映射图像缓冲区到内存
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        g_print("Error: Failed to map buffer\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // 9. 获取图像数据指针
    data = map.data;
    
    // 10. 调用用户回调函数处理图像数据
    if (decoder->callback) {
        decoder->callback(width, height, format, data, map.size, decoder->user_data);
    } else {
        // 如果没有设置回调，打印图像信息
        g_print("width: %d, height: %d, format: %s, data size: %zu bytes\n", 
                width, height, format, map.size);
    }
    
    // 11. 解除内存映射
    gst_buffer_unmap(buf, &map);
    
    // 12. 释放采样数据对象
    gst_sample_unref(sample);
    
    // 13. 返回处理成功
    return GST_FLOW_OK;
}

/**
 * @brief 创建一个新的解码器实例（多路：每路一个）
 * @param rtsp_url RTSP流地址
 * @param callback 图像数据回调函数
 * @param user_data 回调用户数据
 * @return 实例指针
 */
GstDecoder *gst_decoder_create(const gchar *rtsp_url, GstImageCallback callback, void *user_data)
{
    //g_new0 是 GLib 库 提供的一个宏，用于动态分配内存并初始化为零
    GstDecoder *decoder = g_new0(GstDecoder, 1);
    GError *error = NULL;
    gchar *pipeline_str = NULL;

    // 1. 初始化解码器实例
    decoder->callback = callback;
    decoder->user_data = user_data;

    // 2. 构建GStreamer管道字符串
    pipeline_str = g_strdup_printf("rtspsrc location=%s latency=200 ! "
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
    
    gst_element_set_state(dec->pipeline, GST_STATE_NULL);

    // 释放appsink元素
    gst_object_unref(dec->appsink);
    dec->appsink = NULL;
    // 释放GStreamer管道资源
    gst_object_unref(dec->pipeline);
    dec->pipeline = NULL;

    // 释放解码器实例
    g_free(dec);
    
    g_print("GStreamer pipeline destroyed\n");
}