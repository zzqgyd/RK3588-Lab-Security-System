#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include <gst/gst.h>
#include <stdint.h>

#define MAX_CHANNEL 4

// ===== 前向声明（告诉编译器 GstDecoder 是一个类型，但不关心内部结构）=====
struct GstDecoder;
typedef struct GstDecoder GstDecoder;

/**
 * @brief 统一帧结构（贯穿整个系统）
 * 设计目标：
 * 1. 解码 → 推理 → 显示 全部用这个结构
 * 2. 后续支持 zero-copy / DMABUF
 * 3. 生命周期清晰（后续接内存池）
 */
struct Frame
{
    image_buffer_t img;   // 直接复用你已有结构
    uint64_t pts;         // 时间戳（用于同步/丢帧策略）
    uint64_t seq;         // 帧序号
    int stream_id;        // 哪一路流（多路必须有）
    Frame()
    {
        pts = 0;
        seq = 0;
        stream_id = 0;
    }
};

// ===== 通道上下文（每路一个）=====
typedef struct {
    int                  id;            ///< 流ID（0,1,2...）
    GstDecoder          *decoder;       ///< GStreamer解码器实例
    char                 rtsp[256];     ///< RTSP流地址
    image_buffer_t       frame_buf;     ///< 用于显示合成的最新帧缓存
    GMutex               frame_lock;    ///< 保护 frame_buf 的互斥锁
} ChannelContext;

// ===== 应用配置 =====
typedef struct {
    char                 model_path[256];           ///< YOLO模型路径
    ChannelContext       channels[MAX_CHANNEL];     ///< 通道数组
    int                  channel_count;             ///< 实际通道数
} AppConfig;

// 全局配置（在 demo.cpp 中定义）
extern AppConfig g_cfg;

#endif