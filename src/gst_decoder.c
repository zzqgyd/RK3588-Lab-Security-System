#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>

// 这些大写宏 = 安全的类型强制转换！
// 为了兼容 GLib 面向对象体系

// 全局变量定义
static GstElement *pipeline = NULL; // GStreamer管道对象
static GstElement *appsink = NULL;   // 应用程序数据输出口
static GMainLoop *main_loop = NULL;   // 主事件循环
static gboolean is_initialized = FALSE; // 初始化状态

// 图像数据回调函数类型     上层只要实现并传入这个具体的函数
typedef void (*GstImageCallback)(gint width, gint height, const gchar *format, 
                                 gpointer data, size_t data_size, gpointer user_data);

// 回调函数和用户数据
static GstImageCallback image_callback = NULL;
static gpointer callback_user_data = NULL;

/**
 * @brief 当GStreamer解码出一帧数据时执行的回调函数
 * @param sink appsink元素指针
 * @param user_data 用户自定义数据
 * @return 处理结果，返回GST_FLOW_OK表示处理成功
 */
static GstFlowReturn new_sample_cb(GstElement *sink, gpointer user_data)
{
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
    if (image_callback) {
        image_callback(width, height, format, data, map.size, callback_user_data);
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
 * @brief 初始化GStreamer管道           最重要！！！！！！！！！！！！！！！
 * @param rtsp_url RTSP流地址
 * @param callback 图像数据回调函数
 * @param user_data 回调函数的用户数据
 * @return 初始化结果，0表示成功，-1表示失败
 */
int gst_decoder_init(const char *rtsp_url, GstImageCallback callback, gpointer user_data)
{
    GError *error = NULL;       // 错误信息
    gchar *pipeline_str = NULL; // 管道描述字符串
    
    // 检查是否已经初始化
    if (is_initialized) {
        g_print("Error: GStreamer decoder already initialized\n");
        return -1;
    }
    
    // 1. 初始化GStreamer
    gst_init(NULL, NULL);
    
    // 2. 创建主事件循环
    main_loop = g_main_loop_new(NULL, FALSE);
    if (!main_loop) {
        g_print("Error: Failed to create main loop\n");
        return -1;
    }
    
    // 3. 保存回调函数和用户数据
    image_callback = callback;
    callback_user_data = user_data;
    
    // 4.构建 GStreamer 流水线字符串（瑞芯微平台 RTSP拉流 + 硬解码 + AI推理 + 本地显示）
    pipeline_str = g_strdup_printf(
        // 1. RTSP源拉流：设置低延迟latency=200，传入RTSP地址
        "rtspsrc location=%s latency=200 ! "
        // 2. 解RTP封装 + H264解析：将网络流还原成标准H264码流
        "rtph264depay ! h264parse ! "
        // 3. 瑞芯微硬解码：输出NV12格式，开启快速模式，关闭AFBC（避免格式不兼容）
        "mppvideodec format=NV12 fast-mode=true arm-afbc=false ! "
        // 4. 格式转换：转换成RGB格式（AI推理通用格式）
        "videoconvert ! video/x-raw,format=RGB ! "
        // 5. 分流器：将1路视频分成2路，一路给AI推理，一路给屏幕显示
        // "tee name=t ! "
        // -------------------------- 分支1：AI推理分支（低延迟核心）--------------------------
        // 6. 队列缓冲：仅缓存1帧，推理跟不上直接丢帧，保证低延迟、不堆积、不阻塞主线
        // "queue max-size-buffers=1 drop=true ! "
        // 7. 应用程序取帧：关闭时间同步，仅缓存1帧，丢旧保新，让C++代码获取视频帧做推理
        "appsink name=appsink sync=false max-buffers=1 drop=true ",
        // -------------------------- 分支2：屏幕显示分支 --------------------------
        // 8. 连接tee的第二个输出口
        // "t. ! "
        // 9. 显示队列 + 格式转换 + 自动视频渲染：关闭同步，流畅显示不卡顿
        // "queue ! videoconvert ! autovideosink sync=false",
        rtsp_url  // 传入RTSP视频流地址
    );
    
    // 5. 创建GStreamer管道
    pipeline = gst_parse_launch(pipeline_str, &error);
    g_free(pipeline_str);
    
    if (!pipeline) {
        g_print("Error: Failed to create pipeline: %s\n", error->message);
        g_error_free(error);
        g_main_loop_unref(main_loop);
        main_loop = NULL;
        return -1;
    }
    
    // 6. 获取appsink元素   通过"appsink"得到pipeline处理后得到的数据
    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink");
    if (!appsink) {
        g_print("Error: Failed to get appsink element\n");
        gst_object_unref(pipeline);
        pipeline = NULL;
        g_main_loop_unref(main_loop);
        main_loop = NULL;
        return -1;
    }
    
    // 7. 设置appsink属性   appsink有输出，通知     参数固定
    g_object_set(appsink, "emit-signals", TRUE, NULL);
    
    // 8. 连接新样本信号到回调函数  
    // main-loop循环检测到通知如果和new-sample匹配调用回调函数      "new-sample" 是固定信号名
    //GStreamer 所有元素 默认都在 main loop 里。    所以也不用手动添加
    g_signal_connect(G_OBJECT(appsink), "new-sample", 
                     G_CALLBACK(new_sample_cb), NULL);
    
    // 标记初始化成功
    is_initialized = TRUE;
    
    return 0;
}

/**
 * @brief 启动GStreamer管道
 * @return 启动结果，0表示成功，-1表示失败
 */
int gst_decoder_start(void)
{
    // 检查是否已经初始化
    if (!is_initialized) {
        g_print("Error: GStreamer decoder not initialized\n");
        return -1;
    }
    
    // 检查管道是否存在
    if (!pipeline) {
        g_print("Error: Pipeline not created\n");
        return -1;
    }
    
    // 启动管道
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print("Error: Failed to start pipeline\n");
        return -1;
    }
    
    g_print("Pipeline started.\n");
    return 0;
}
/**
 * @brief 运行GStreamer主事件循环
 * @note 此函数会阻塞当前线程，直到调用gst_decoder_stop()
 */
void gst_decoder_run(void)
{
    // 检查是否已经初始化
    if (!is_initialized || !main_loop) {
        g_print("Error: GStreamer decoder not initialized\n");
        return;
    }
    
    // 运行主事件循环
    // 类似while (1) {
    //     检查有没有消息；
    //     如果有消息 → 调用对应的函数；
    //     没有消息 → 等待；
    // }
    g_main_loop_run(main_loop);
}

/**
 * @brief 停止GStreamer管道
 * @return 停止结果，0表示成功，-1表示失败
 */
int gst_decoder_stop(void)
{
    // 检查是否已经初始化
    if (!is_initialized) {
        g_print("Error: GStreamer decoder not initialized\n");
        return -1;
    }
    
    // 停止主事件循环
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
    
    // 停止管道
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
    
    g_print("Pipeline stopped.\n");
    return 0;
}

/**
 * @brief 清理GStreamer资源
 * @return 清理结果，0表示成功，-1表示失败
 */
int gst_decoder_cleanup(void)
{
    // 检查是否已经初始化
    if (!is_initialized) {
        g_print("Error: GStreamer decoder not initialized\n");
        return -1;
    }
    
    // 清理资源
    if (appsink) {
        gst_object_unref(appsink);
        appsink = NULL;
    }
    
    if (pipeline) {
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
    
    if (main_loop) {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }
    
    // 重置回调函数和用户数据
    image_callback = NULL;
    callback_user_data = NULL;
    
    // 标记未初始化
    is_initialized = FALSE;
    
    g_print("GStreamer decoder cleanup done.\n");
    return 0;
}

/**
 * @brief 检查GStreamer解码器是否已初始化
 * @return 是否已初始化，TRUE表示已初始化，FALSE表示未初始化
 */
gboolean gst_decoder_is_initialized(void)
{
    return is_initialized;
}