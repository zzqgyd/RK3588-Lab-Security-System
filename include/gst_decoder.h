#ifndef GST_DECODER_H
#define GST_DECODER_H

/**
 * @file gst_decoder.h
 * @brief GStreamer解码器对外接口（多路安全实例版）
 */

#ifdef __cplusplus
extern "C" {
#endif

// 图像数据回调函数类型
typedef void (*GstImageCallback)(int width, int height, const char *format,
                                 void *data, size_t data_size, void *user_data);

// 前向声明：解码器实例句柄（对外隐藏）
typedef struct GstDecoder GstDecoder;

/**
 * @brief 创建一个新的解码器实例（多路：每路一个）
 * @param rtsp_url RTSP流地址
 * @param callback 图像数据回调函数
 * @param user_data 回调用户数据（通常传通道上下文）
 * @return 解码器实例指针，失败返回NULL
 */
GstDecoder *gst_decoder_create(const gchar *rtsp_url, GstImageCallback callback, void *user_data);

/**
 * @brief 启动解码器
 * @param dec 解码器实例
 * @return 0成功，-1失败
 */
int gst_decoder_start(GstDecoder *dec);

/**
 * @brief 停止解码器
 * @param dec 解码器实例
 * @return 0成功，-1失败
 */
int gst_decoder_stop(GstDecoder *dec);

/**
 * @brief 销毁解码器实例，释放所有资源
 * @param dec 解码器实例
 */
void gst_decoder_destroy(GstDecoder *dec);

#ifdef __cplusplus
}
#endif

#endif // GST_DECODER_H