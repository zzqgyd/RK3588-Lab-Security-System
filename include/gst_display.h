#ifndef __GST_DISPLAY_H__
#define __GST_DISPLAY_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化显示管道
int gst_display_init(int width, int height);

// 推送一帧RGB图像到屏幕显示
void gst_display_push_rgb(int width, int height, uint8_t *rgb_data, size_t size);

// 销毁释放
void gst_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif