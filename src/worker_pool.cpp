#include "worker_pool.hpp"
#include "yolov5_infer.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <chrono>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include "im2d.h"
#include "RgaUtils.h"

extern AppConfig g_cfg;

WorkerPool::WorkerPool(int num_workers,
                       MPMCQueue<FramePtr>* in_queue,
                       worker_context_t* worker_ctxs,
                       ChannelContext* channels,
                       int channel_count)
    : num_workers_(num_workers)
    , in_queue_(in_queue)
    , worker_ctxs_(worker_ctxs)
    , channels_(channels)
    , channel_count_(channel_count)
    , running_(false)
{
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::start()
{
    if (running_) return;
    running_ = true;
    
    for (int i = 0; i < num_workers_; i++) {
        threads_.emplace_back(&WorkerPool::worker_loop, this, i);
    }
    printf("[WorkerPool] Started %d workers\n", num_workers_);
}

void WorkerPool::stop()
{
    if (!running_) return;
    running_ = false;
    
    for (auto& th : threads_) {
        if (th.joinable()) {
            th.join();
        }
    }
    threads_.clear();
    printf("[WorkerPool] All workers stopped\n");
}

void WorkerPool::worker_loop(int worker_id)
{
    worker_context_t* worker_ctx = &worker_ctxs_[worker_id];
    FramePtr frame = nullptr;
    
    printf("[Worker %d] Started\n", worker_id);

    while (running_)
    {
        if (in_queue_->try_dequeue(frame))
        {
            process_frame(worker_ctx, frame);
        }
        else
        {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 清空剩余帧（shared_ptr 自动释放）
    printf("[Worker %d] Draining remaining frames...\n", worker_id);
    while (in_queue_->try_dequeue(frame))
    {
        // frame 出队后引用计数 -1，自动释放
    }
    
    printf("[Worker %d] Stopped\n", worker_id);
}


void WorkerPool::process_frame(worker_context_t* worker_ctx, FramePtr frame)
{
    if (!frame || !frame->img.virt_addr) return;
    
    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));
    
    // 1. 推理
    int ret = yolov5_infer(worker_ctx, &frame->img, &od_results);
    if (ret != 0) return;
    
    // 2. CPU 画框
    if (od_results.count > 0) {
        letterbox_t* lb = &worker_ctx->last_letter_box;
        
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result* res = &od_results.results[i];
            
            // 使用实际的 padding 和 scale
            int x = (res->box.left - lb->x_pad) / lb->scale;
            int y = (res->box.top - lb->y_pad) / lb->scale;
            int w = (res->box.right - res->box.left) / lb->scale;
            int h = (res->box.bottom - res->box.top) / lb->scale;
            
            // 强制偶数对齐（NV12 要求）
            x = x & ~1;  // 向下对齐到偶数
            y = y & ~1;
            w = (w + 1) & ~1;  // 向上对齐到偶数
            h = (h + 1) & ~1;

            // 边界裁剪
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > frame->img.width)  w = frame->img.width - x;
            if (y + h > frame->img.height) h = frame->img.height - y;

            // 再次确保偶数和最小尺寸
            w = (w < 4) ? 4 : (w & ~1);
            h = (h < 4) ? 4 : (h & ~1);
            
            if (w < 4 || h < 4) continue;
            
            draw_rectangle_yuv420sp(
                frame->img.virt_addr,
                frame->img.width,
                frame->img.height,
                x, y, w, h,
                COLOR_RED,
                2
            );
        }
    }
    
    // 3. 更新显示缓存
    ChannelContext* ch = &channels_[frame->stream_id];
    ch->set_frame(frame);
}