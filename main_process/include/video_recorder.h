/**
 * @file video_recorder.h
 * @brief 录像模块接口 - 基于 FFmpeg h264_rkmpp 硬件编码
 * 
 * 设计原则：
 * 1. 接口稳定，内部实现可替换
 * 2. DMA-BUF fd 由外部管理生命周期，录像器不持有 fd 所有权
 * 3. 录像器内部通过 AVBufferRef 引用计数管理编码帧
 */

#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 录像器句柄（不透明指针）
 * 
 * 对外隐藏 FFmpeg 实现细节，调用者只需通过 API 操作
 */
typedef struct VideoRecorder VideoRecorder;

/**
 * @brief 创建录像器并初始化 h264_rkmpp 硬件编码器
 * 
 * 内部流程：
 *   1. 创建 RKMPP 硬件设备上下文
 *   2. 查找并打开 h264_rkmpp 编码器
 *   3. 设置编码参数：分辨率、码率、GOP、DRM_PRIME 输入
 * 
 * @param output_dir  录像文件输出目录（会自动创建）
 * @param stream_id   流ID（用于文件命名：stream{id}_{timestamp}.mp4）
 * @param width       视频宽度（像素）
 * @param height      视频高度（像素）
 * @param fmt         保留参数（兼容旧接口，实际固定为 H.264）
 * @return            录像器指针，失败返回 NULL
 */
VideoRecorder* vr_create(const char* output_dir, int stream_id,
                         int width, int height, int fmt);

/**
 * @brief 开始录像：创建 MP4 文件、写入文件头（含 SPS/PPS）
 * 
 * 内部流程：
 *   1. 生成文件名
 *   2. 创建 MP4 封装上下文
 *   3. 添加视频流，复制编码器参数
 *   4. 打开输出文件
 *   5. 写入 MP4 文件头（avformat_write_header）
 * 
 * @param vr              录像器句柄
 * @param start_time_str  开始时间字符串（格式："20260528_143025"，用于文件命名）
 * @return                0 成功，-1 失败
 */
int vr_start(VideoRecorder* vr, const char* start_time_str);

/**
 * @brief 喂入一帧 NV12 数据到编码器
 * 
 * 数据源优先级：fd >= 0 则使用 DMA-BUF 路径（零拷贝），否则使用 nv12_data
 * 
 * DMA-BUF 路径说明：
 *   - fd 由调用者持有所有权（GStreamer buffer 的 dmabuf fd）
 *   - 录像器内部会通过 AVBufferRef 引用帧数据
 *   - 调用者应通过 FramePtr 引用计数保证 fd 在编码期间有效
 *   - 编码完成后 AVBufferRef 释放，不再引用 fd
 * 
 * virt_addr 路径说明（回退方案）：
 *   - 录像器内部 av_frame_get_buffer 分配内存
 *   - memcpy 拷贝数据，不依赖外部 buffer 生命周期
 * 
 * @param vr         录像器句柄
 * @param nv12_data  虚拟地址（DMA-BUF 路径可为 NULL）
 * @param fd         DMA-BUF 文件描述符（-1 表示使用 nv12_data 回退路径）
 * @param size       数据大小（字节，用于 DMA-BUF 路径的 buffer size）
 * @param pts        时间戳（未使用，PTS 由 frame_counter 自增生成）
 * @return           0 成功，-1 失败
 */
int vr_feed_frame(VideoRecorder* vr, const uint8_t* nv12_data, int fd,
                  size_t size, uint64_t pts);

/**
 * @brief 停止录像：清空编码器缓冲区、写入文件尾、关闭文件
 * 
 * 内部流程：
 *   1. avcodec_send_frame(ctx, NULL) 启动 draining
 *   2. 循环 avcodec_receive_packet 接收剩余编码包
 *   3. av_write_trailer 回填 moov box（时长、帧偏移等）
 *   4. 关闭文件 IO，释放封装上下文
 * 
 * @param vr            录像器句柄
 * @param end_time_str  结束时间字符串（保留参数，暂未使用）
 * @return              录像文件完整路径（调用者需 free），失败返回 NULL
 */
char* vr_stop(VideoRecorder* vr, const char* end_time_str);

/**
 * @brief 销毁录像器，释放所有 FFmpeg 资源
 * 
 * 释放顺序（与创建顺序相反）：
 *   1. 如果仍在录像，先停止（vr_stop）
 *   2. 释放编码器上下文（avcodec_free_context）
 *   3. 释放硬件设备上下文（av_buffer_unref）
 *   4. 释放结构体本身
 * 
 * @param vr  录像器句柄
 */
void vr_destroy(VideoRecorder* vr);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_RECORDER_H