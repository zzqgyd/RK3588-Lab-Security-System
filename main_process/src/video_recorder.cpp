/**
 * @file video_recorder.cpp
 * @brief 录像模块实现 - 修复段错误问题
 */

#include "video_recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libdrm/drm_fourcc.h>
#include <libavutil/hwcontext_drm.h>
}

/* ================================================================
 * 常量定义
 * ================================================================ */
#define VR_TIME_BASE_DEN    90000
#define VR_DEFAULT_FPS      25
#define VR_TICKS_PER_FRAME  (VR_TIME_BASE_DEN / VR_DEFAULT_FPS)
#define VR_DEFAULT_GOP      60
#define VR_MAX_ERROR_MSG    256

/* ================================================================
 * 录像器内部结构体 - 添加线程安全保护
 * ================================================================ */
struct VideoRecorder {
    char      output_dir[256];
    int       stream_id;
    int       width;
    int       height;
    bool      recording;

    char      file_path[512];

    AVFormatContext* ofmt_ctx;
    AVCodecContext*  enc_ctx;
    AVBufferRef*     hw_ctx;
    int              out_stream_idx;

    int64_t          frame_counter;
    int              gop_size;
    
    // ===== 新增：保护编码器访问的锁 =====
    // 虽然 per-stream 架构保证了单线程访问，
    // 但为安全起见，添加互斥锁保护编码器操作
    pthread_mutex_t  enc_lock;
    
    // ===== 新增：错误计数，连续错误超过阈值则停止录像 =====
    int              error_count;
    static const int MAX_ERRORS = 10;
};

/* ================================================================
 * 释放回调
 * 注意：opaque 持有 dup 出来的 fd，释放时必须 close
 * ================================================================ */
static void free_drm_frame_desc(void *opaque, uint8_t *data)
{
    // opaque 存的是 dup 出来的 fd（int*），需要 close
    if (opaque) {
        int* pfd = (int*)opaque;
        if (*pfd >= 0) {
            close(*pfd);
        }
        av_free(opaque);
    }
    // 释放 desc 内存
    if (data) {
        av_free(data);
    }
}

/* ================================================================
 * drain_pending_packets: 排空编码器
 * ================================================================ */
static int drain_pending_packets(VideoRecorder *vr)
{
    if (!vr || !vr->enc_ctx || !vr->ofmt_ctx) {
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "[VR] Failed to alloc packet for drain\n");
        return -1;
    }

    int ret;
    int packet_count = 0;

    while (1) {
        ret = avcodec_receive_packet(vr->enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN)) {
            break;  // 需要更多输入
        }
        if (ret == AVERROR_EOF) {
            break;  // 编码器已关闭
        }
        if (ret < 0) {
            char errbuf[VR_MAX_ERROR_MSG];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "[VR] Stream %d: drain_packet error: %s\n", 
                   vr->stream_id, errbuf);
            av_packet_free(&pkt);
            return -1;
        }

        // 写入文件
        pkt->stream_index = vr->out_stream_idx;
        
        if (vr->ofmt_ctx && vr->ofmt_ctx->streams[vr->out_stream_idx]) {
            av_packet_rescale_ts(pkt, vr->enc_ctx->time_base,
                                 vr->ofmt_ctx->streams[vr->out_stream_idx]->time_base);
            int write_ret = av_interleaved_write_frame(vr->ofmt_ctx, pkt);
            if (write_ret < 0) {
                char errbuf[VR_MAX_ERROR_MSG];
                av_strerror(write_ret, errbuf, sizeof(errbuf));
                fprintf(stderr, "[VR] Stream %d: write error: %s\n",
                       vr->stream_id, errbuf);
            } else {
                packet_count++;
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return packet_count;
}

/* ================================================================
 * configure_h264_params
 * ================================================================ */
static void configure_h264_params(AVCodecContext *ctx)
{
    av_opt_set(ctx->priv_data, "profile", "high", 0);
    av_opt_set(ctx->priv_data, "level", "4.0", 0);
    av_opt_set(ctx->priv_data, "coder", "1", 0);
    av_opt_set(ctx->priv_data, "trans8x8", "1", 0);
    
    // ===== 添加：设置编码器输入超时和异步深度 =====
    av_opt_set_int(ctx->priv_data, "async_depth", 4, 0);
}

/* ================================================================
 * vr_create: 创建录像器
 * ================================================================ */
VideoRecorder* vr_create(const char* output_dir, int stream_id,
                         int width, int height, int fmt)
{
    (void)fmt;

    VideoRecorder* vr = (VideoRecorder*)calloc(1, sizeof(VideoRecorder));
    if (!vr) {
        fprintf(stderr, "[VR] Failed to allocate VideoRecorder\n");
        return NULL;
    }

    // 初始化互斥锁
    pthread_mutex_init(&vr->enc_lock, NULL);

    strncpy(vr->output_dir, output_dir, sizeof(vr->output_dir) - 1);
    vr->output_dir[sizeof(vr->output_dir) - 1] = '\0';
    vr->stream_id     = stream_id;
    vr->width         = width;
    vr->height        = height;
    vr->recording     = false;
    vr->gop_size      = VR_DEFAULT_GOP;
    vr->frame_counter = 0;
    vr->ofmt_ctx      = NULL;
    vr->enc_ctx       = NULL;
    vr->hw_ctx        = NULL;
    vr->file_path[0]  = '\0';
    vr->out_stream_idx = -1;
    vr->error_count   = 0;

    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&vr->hw_ctx, AV_HWDEVICE_TYPE_RKMPP,
                                      NULL, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "[VR] Stream %d: Failed to create RKMPP device\n", stream_id);
        pthread_mutex_destroy(&vr->enc_lock);
        free(vr);
        return NULL;
    }

    // 查找编码器
    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!encoder) {
        fprintf(stderr, "[VR] Stream %d: h264_rkmpp not found\n", stream_id);
        av_buffer_unref(&vr->hw_ctx);
        pthread_mutex_destroy(&vr->enc_lock);
        free(vr);
        return NULL;
    }

    vr->enc_ctx = avcodec_alloc_context3(encoder);
    if (!vr->enc_ctx) {
        fprintf(stderr, "[VR] Stream %d: Failed to alloc encoder ctx\n", stream_id);
        av_buffer_unref(&vr->hw_ctx);
        pthread_mutex_destroy(&vr->enc_lock);
        free(vr);
        return NULL;
    }

    // ===== 关键修复：确保分辨率是16的倍数（H.264要求） =====
    int aligned_width = FFALIGN(width, 16);
    int aligned_height = FFALIGN(height, 16);
    
    if (aligned_width != width || aligned_height != height) {
        printf("[VR] Stream %d: Aligning resolution %dx%d → %dx%d\n",
               stream_id, width, height, aligned_width, aligned_height);
    }

    // 设置编码参数
    vr->enc_ctx->width       = aligned_width;
    vr->enc_ctx->height      = aligned_height;
    vr->enc_ctx->pix_fmt     = AV_PIX_FMT_DRM_PRIME;
    vr->enc_ctx->sw_pix_fmt  = AV_PIX_FMT_NV12;
    vr->enc_ctx->time_base    = (AVRational){1, VR_TIME_BASE_DEN};
    vr->enc_ctx->framerate    = (AVRational){VR_DEFAULT_FPS, 1};
    vr->enc_ctx->pkt_timebase = (AVRational){1, VR_TIME_BASE_DEN};

    // ===== 修复：降低码率，使用更保守的值 =====
    vr->enc_ctx->bit_rate     = (int64_t)aligned_width * aligned_height / 2;
    vr->enc_ctx->rc_max_rate  = vr->enc_ctx->bit_rate * 2;
    vr->enc_ctx->rc_buffer_size = vr->enc_ctx->bit_rate * 4;
    vr->enc_ctx->gop_size     = vr->gop_size;
    vr->enc_ctx->max_b_frames = 0;
    vr->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    configure_h264_params(vr->enc_ctx);

    vr->enc_ctx->hw_device_ctx = av_buffer_ref(vr->hw_ctx);
    if (!vr->enc_ctx->hw_device_ctx) {
        fprintf(stderr, "[VR] Stream %d: Failed to ref hw ctx\n", stream_id);
        avcodec_free_context(&vr->enc_ctx);
        av_buffer_unref(&vr->hw_ctx);
        pthread_mutex_destroy(&vr->enc_lock);
        free(vr);
        return NULL;
    }

    ret = avcodec_open2(vr->enc_ctx, encoder, NULL);
    if (ret < 0) {
        char errbuf[VR_MAX_ERROR_MSG];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[VR] Stream %d: Failed to open encoder: %s\n", stream_id, errbuf);
        avcodec_free_context(&vr->enc_ctx);
        av_buffer_unref(&vr->hw_ctx);
        pthread_mutex_destroy(&vr->enc_lock);
        free(vr);
        return NULL;
    }

    printf("[VR] Stream %d: Encoder created (%dx%d)\n", stream_id, aligned_width, aligned_height);
    return vr;
}

/* ================================================================
 * recreate_encoder: 销毁旧编码器、创建新编码器
 *
 * 为什么需要：rkmpp 硬件编码器不支持 avcodec_flush_buffers，
 *   vr_stop 末尾的 avcodec_send_frame(NULL) 会让编码器进入 EOF 状态，
 *   无法通过 flush 重置。唯一可靠的办法是销毁重建。
 * 调用时机：每次 vr_start 开头
 * ================================================================ */
static int recreate_encoder(VideoRecorder* vr)
{
    if (!vr) return -1;

    // 1. 销毁旧编码器上下文
    if (vr->enc_ctx) {
        avcodec_free_context(&vr->enc_ctx);
        vr->enc_ctx = NULL;
    }
    // hw_ctx 保留不动，可复用（硬件设备上下文与编码器实例无关）

    // 2. 查找编码器
    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!encoder) {
        fprintf(stderr, "[VR] Stream %d: h264_rkmpp not found\n", vr->stream_id);
        return -1;
    }

    // 3. 分配新的编码器上下文
    vr->enc_ctx = avcodec_alloc_context3(encoder);
    if (!vr->enc_ctx) {
        fprintf(stderr, "[VR] Stream %d: Failed to alloc encoder ctx\n", vr->stream_id);
        return -1;
    }

    // 4. 设置编码参数（与 vr_create 一致）
    int aligned_width  = FFALIGN(vr->width, 16);
    int aligned_height = FFALIGN(vr->height, 16);

    vr->enc_ctx->width       = aligned_width;
    vr->enc_ctx->height      = aligned_height;
    vr->enc_ctx->pix_fmt     = AV_PIX_FMT_DRM_PRIME;
    vr->enc_ctx->sw_pix_fmt  = AV_PIX_FMT_NV12;
    vr->enc_ctx->time_base    = (AVRational){1, VR_TIME_BASE_DEN};
    vr->enc_ctx->framerate    = (AVRational){VR_DEFAULT_FPS, 1};
    vr->enc_ctx->pkt_timebase = (AVRational){1, VR_TIME_BASE_DEN};

    vr->enc_ctx->bit_rate     = (int64_t)aligned_width * aligned_height / 2;
    vr->enc_ctx->rc_max_rate  = vr->enc_ctx->bit_rate * 2;
    vr->enc_ctx->rc_buffer_size = vr->enc_ctx->bit_rate * 4;
    vr->enc_ctx->gop_size     = vr->gop_size;
    vr->enc_ctx->max_b_frames = 0;
    vr->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    configure_h264_params(vr->enc_ctx);

    // 5. 绑定硬件设备上下文（复用 vr->hw_ctx）
    vr->enc_ctx->hw_device_ctx = av_buffer_ref(vr->hw_ctx);
    if (!vr->enc_ctx->hw_device_ctx) {
        fprintf(stderr, "[VR] Stream %d: Failed to ref hw ctx\n", vr->stream_id);
        avcodec_free_context(&vr->enc_ctx);
        return -1;
    }

    // 6. 打开编码器
    int ret = avcodec_open2(vr->enc_ctx, encoder, NULL);
    if (ret < 0) {
        char errbuf[VR_MAX_ERROR_MSG];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[VR] Stream %d: Failed to open encoder: %s\n",
                vr->stream_id, errbuf);
        avcodec_free_context(&vr->enc_ctx);
        return -1;
    }

    return 0;
}

/* ================================================================
 * vr_start: 开始录像
 * ================================================================ */
int vr_start(VideoRecorder* vr, const char* start_time_str)
{
    if (!vr) return -1;
    if (vr->recording) return -1;

    // 重置错误计数
    vr->error_count = 0;

    // ★ 关键修复：销毁旧编码器、重建新编码器
    //   原因：vr_stop 末尾调用 avcodec_send_frame(NULL) flush 编码器，
    //         这会让 rkmpp 编码器进入 EOF 状态。
    //         rkmpp 不支持 avcodec_flush_buffers（会被忽略），
    //         唯一可靠的办法是销毁重建编码器实例。
    if (recreate_encoder(vr) != 0) {
        fprintf(stderr, "[VR] Stream %d: recreate_encoder failed\n", vr->stream_id);
        return -1;
    }

    mkdir(vr->output_dir, 0777);

    char safe_time[64];
    strncpy(safe_time, start_time_str, sizeof(safe_time) - 1);
    safe_time[sizeof(safe_time) - 1] = '\0';
    for (char* p = safe_time; *p; p++) {
        if (*p == ':' || *p == ' ') *p = '_';
    }

    snprintf(vr->file_path, sizeof(vr->file_path),
             "%s/stream%d_%s.mp4",
             vr->output_dir, vr->stream_id, safe_time);

    vr->frame_counter = 0;

    int ret = avformat_alloc_output_context2(&vr->ofmt_ctx, NULL, NULL,
                                              vr->file_path);
    if (ret < 0 || !vr->ofmt_ctx) {
        fprintf(stderr, "[VR] Stream %d: Failed to create output ctx\n", vr->stream_id);
        return -1;
    }

    AVStream* out_stream = avformat_new_stream(vr->ofmt_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "[VR] Stream %d: Failed to create output stream\n", vr->stream_id);
        avformat_free_context(vr->ofmt_ctx);
        vr->ofmt_ctx = NULL;
        return -1;
    }
    vr->out_stream_idx = out_stream->index;

    ret = avcodec_parameters_from_context(out_stream->codecpar, vr->enc_ctx);
    if (ret < 0) {
        fprintf(stderr, "[VR] Stream %d: Failed to copy codec params\n", vr->stream_id);
        avformat_free_context(vr->ofmt_ctx);
        vr->ofmt_ctx = NULL;
        return -1;
    }

    out_stream->time_base = vr->enc_ctx->time_base;

    if (!(vr->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&vr->ofmt_ctx->pb, vr->file_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "[VR] Stream %d: Cannot open file: %s\n",
                    vr->stream_id, vr->file_path);
            avformat_free_context(vr->ofmt_ctx);
            vr->ofmt_ctx = NULL;
            return -1;
        }
    }

    // ===== 新增：fragmented MP4，崩溃也能播放 =====
    av_opt_set(vr->ofmt_ctx->priv_data, "movflags", 
               "frag_keyframe+empty_moov+default_base_moof", 0);

    ret = avformat_write_header(vr->ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "[VR] Stream %d: Failed to write header\n", vr->stream_id);
        avio_closep(&vr->ofmt_ctx->pb);
        avformat_free_context(vr->ofmt_ctx);
        vr->ofmt_ctx = NULL;
        return -1;
    }

    vr->recording = true;
    printf("[VR] Stream %d: Recording started → %s\n", vr->stream_id, vr->file_path);
    return 0;
}

/* ================================================================
 * vr_feed_frame: 喂入一帧 - 修复版本
 * ================================================================ */
int vr_feed_frame(VideoRecorder* vr, const uint8_t* nv12_data, int fd,
                  size_t size, uint64_t pts)
{
    (void)pts;

    if (!vr) return -1;
    if (!vr->recording) return 0;
    if (!vr->enc_ctx || !vr->ofmt_ctx) return -1;

    // ===== 检查错误计数 =====
    if (vr->error_count >= VideoRecorder::MAX_ERRORS) {
        fprintf(stderr, "[VR] Stream %d: Too many errors, stopping feed\n", vr->stream_id);
        return -1;
    }

    // ===== 加锁保护编码器访问 =====
    pthread_mutex_lock(&vr->enc_lock);

    // 先排空上次的包
    int drain_ret = drain_pending_packets(vr);
    if (drain_ret < 0) {
        vr->error_count++;
        pthread_mutex_unlock(&vr->enc_lock);
        return -1;
    }

    // 分配 AVFrame
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        vr->error_count++;
        pthread_mutex_unlock(&vr->enc_lock);
        return -1;
    }

    frame->width  = vr->enc_ctx->width;
    frame->height = vr->enc_ctx->height;
    frame->format = AV_PIX_FMT_DRM_PRIME;
    frame->pts = vr->frame_counter * VR_TICKS_PER_FRAME;
    vr->frame_counter++;

    int ret = 0;

    if (fd >= 0) {
        // DMA-BUF 路径
        // ★ 关键修复：dup fd，让 AVBufferRef 持有自己的 fd 拷贝
        //   原因：rkmpp 编码器 async_depth=4 会内部缓存帧，task.frame 析构后
        //         原始 fd 会被 gst_sample_unref 关闭，导致编码器访问已关闭的 fd。
        //         dup 出来的 fd 由 free_drm_frame_desc 回调负责 close，
        //         生命周期完全由 AVBufferRef 引用计数管理，与外部 FramePtr 解耦。
        int dup_fd = dup(fd);
        if (dup_fd < 0) {
            fprintf(stderr, "[VR] Stream %d: dup fd failed\n", vr->stream_id);
            av_frame_free(&frame);
            vr->error_count++;
            pthread_mutex_unlock(&vr->enc_lock);
            return -1;
        }

        // 分配一个 int 存 dup_fd，由 opaque 传给释放回调
        int* pfd = (int*)av_mallocz(sizeof(int));
        if (!pfd) {
            close(dup_fd);
            av_frame_free(&frame);
            vr->error_count++;
            pthread_mutex_unlock(&vr->enc_lock);
            return -1;
        }
        *pfd = dup_fd;

        size_t desc_size = sizeof(AVDRMFrameDescriptor);
        AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)av_mallocz(desc_size);
        if (!desc) {
            av_free(pfd);
            close(dup_fd);
            fprintf(stderr, "[VR] Stream %d: Failed to alloc DRM desc\n", vr->stream_id);
            av_frame_free(&frame);
            vr->error_count++;
            pthread_mutex_unlock(&vr->enc_lock);
            return -1;
        }

        desc->nb_objects = 1;
        desc->objects[0].fd = dup_fd;        // ★ 用 dup 出来的 fd
        desc->objects[0].size = size;
        desc->objects[0].format_modifier = DRM_FORMAT_MOD_LINEAR;

        desc->nb_layers = 1;
        desc->layers[0].format = DRM_FORMAT_NV12;
        desc->layers[0].nb_planes = 2;

        desc->layers[0].planes[0].object_index = 0;
        desc->layers[0].planes[0].offset = 0;
        desc->layers[0].planes[0].pitch = vr->enc_ctx->width;

        desc->layers[0].planes[1].object_index = 0;
        desc->layers[0].planes[1].offset = vr->enc_ctx->width * vr->enc_ctx->height;
        desc->layers[0].planes[1].pitch = vr->enc_ctx->width;

        // ★ opaque 传 pfd，释放回调会 close 它
        AVBufferRef *desc_buf = av_buffer_create(
            (uint8_t *)desc, desc_size,
            free_drm_frame_desc, pfd, 0
        );

        if (!desc_buf) {
            fprintf(stderr, "[VR] Stream %d: Failed to create AVBufferRef\n", vr->stream_id);
            av_free(desc);
            av_free(pfd);
            close(dup_fd);
            av_frame_free(&frame);
            vr->error_count++;
            pthread_mutex_unlock(&vr->enc_lock);
            return -1;
        }

        frame->buf[0] = desc_buf;
        frame->data[0] = (uint8_t *)desc;
        frame->linesize[0] = vr->enc_ctx->width;
        frame->linesize[1] = vr->enc_ctx->width;

    } else if (nv12_data) {
        // virt_addr 回退路径
        ret = av_frame_get_buffer(frame, 32);
        if (ret < 0) {
            fprintf(stderr, "[VR] Stream %d: av_frame_get_buffer failed\n", vr->stream_id);
            av_frame_free(&frame);
            vr->error_count++;
            pthread_mutex_unlock(&vr->enc_lock);
            return -1;
        }

        memcpy(frame->data[0], nv12_data, vr->enc_ctx->width * vr->enc_ctx->height);
        memcpy(frame->data[1], nv12_data + vr->enc_ctx->width * vr->enc_ctx->height,
               vr->enc_ctx->width * vr->enc_ctx->height / 2);

        frame->linesize[0] = vr->enc_ctx->width;
        frame->linesize[1] = vr->enc_ctx->width;
    } else {
        fprintf(stderr, "[VR] Stream %d: No valid data\n", vr->stream_id);
        av_frame_free(&frame);
        vr->error_count++;
        pthread_mutex_unlock(&vr->enc_lock);
        return -1;
    }

    // 发送帧
    ret = avcodec_send_frame(vr->enc_ctx, frame);
    av_frame_free(&frame);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char errbuf[VR_MAX_ERROR_MSG];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[VR] Stream %d: send_frame failed: %s\n", vr->stream_id, errbuf);
        vr->error_count++;
        pthread_mutex_unlock(&vr->enc_lock);
        return -1;
    }

    // 尝试接收包
    drain_ret = drain_pending_packets(vr);
    if (drain_ret < 0) {
        vr->error_count++;
    }

    // ===== 解锁 =====
    pthread_mutex_unlock(&vr->enc_lock);
    return 0;
}

/* ================================================================
 * vr_stop: 停止录像
 * ================================================================ */
char* vr_stop(VideoRecorder* vr, const char* end_time_str)
{
    (void)end_time_str;

    if (!vr) return NULL;
    if (!vr->recording) return NULL;
    if (!vr->enc_ctx || !vr->ofmt_ctx) {
        vr->recording = false;
        return NULL;
    }

    printf("[VR] Stream %d: Stopping recording...\n", vr->stream_id);

    pthread_mutex_lock(&vr->enc_lock);

    int ret = avcodec_send_frame(vr->enc_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "[VR] Stream %d: Flush failed\n", vr->stream_id);
    }

    drain_pending_packets(vr);

    ret = av_write_trailer(vr->ofmt_ctx);
    if (ret < 0) {
        fprintf(stderr, "[VR] Stream %d: Write trailer failed\n", vr->stream_id);
    }

    if (vr->ofmt_ctx && !(vr->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&vr->ofmt_ctx->pb);
    }

    avformat_free_context(vr->ofmt_ctx);
    vr->ofmt_ctx = NULL;
    vr->out_stream_idx = -1;

    vr->recording = false;

    pthread_mutex_unlock(&vr->enc_lock);

    printf("[VR] Stream %d: Recording stopped → %s\n", vr->stream_id, vr->file_path);

    char* path = (char*)malloc(strlen(vr->file_path) + 1);
    if (path) {
        strcpy(path, vr->file_path);
    }
    return path;
}

/* ================================================================
 * vr_destroy: 销毁录像器
 * ================================================================ */
void vr_destroy(VideoRecorder* vr)
{
    if (!vr) return;

    if (vr->recording) {
        char* path = vr_stop(vr, "abort");
        if (path) free(path);
    }

    if (vr->enc_ctx) {
        avcodec_free_context(&vr->enc_ctx);
        vr->enc_ctx = NULL;
    }

    if (vr->hw_ctx) {
        av_buffer_unref(&vr->hw_ctx);
        vr->hw_ctx = NULL;
    }

    pthread_mutex_destroy(&vr->enc_lock);
    free(vr);
}