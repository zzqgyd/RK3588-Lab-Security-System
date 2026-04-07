#ifndef GST_DECODER_H
#define GST_DECODER_H

/**
 * @file gst_decoder.h
 * @brief GStreamer解码器对外接口
 */

#ifdef __cplusplus
extern "C" {
#endif

// 图像数据回调函数类型
typedef void (*GstImageCallback)(int width, int height, const char *format, 
                                 void *data, size_t data_size, void *user_data);

/**
 * @brief 初始化GStreamer管道
 * @param rtsp_url RTSP流地址
 * @param callback 图像数据回调函数
 * @param user_data 回调函数的用户数据
 * @return 初始化结果，0表示成功，-1表示失败
 */
int gst_decoder_init(const char *rtsp_url, GstImageCallback callback, void *user_data);

/**
 * @brief 启动GStreamer管道
 * @return 启动结果，0表示成功，-1表示失败
 */
int gst_decoder_start(void);

/**
 * @brief 运行GStreamer主事件循环
 * @note 此函数会阻塞当前线程，直到调用gst_decoder_stop()
 */
void gst_decoder_run(void);

/**
 * @brief 停止GStreamer管道
 * @return 停止结果，0表示成功，-1表示失败
 */
int gst_decoder_stop(void);

/**
 * @brief 清理GStreamer资源
 * @return 清理结果，0表示成功，-1表示失败
 */
int gst_decoder_cleanup(void);

/**
 * @brief 检查GStreamer解码器是否已初始化
 * @return 是否已初始化，1表示已初始化，0表示未初始化
 */
int gst_decoder_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif // GST_DECODER_H
