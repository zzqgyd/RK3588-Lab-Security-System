#include "worker_pool.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// 外部全局配置（在 demo.cpp 中定义）
extern AppConfig g_cfg;
extern Scheduler* g_scheduler;

WorkerPool::WorkerPool(int num_workers,
                       MPMCQueue<Frame*>* in_queue,
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
    Frame* frame = nullptr;
    
    printf("[Worker %d] Started\n", worker_id);
    
    while (running_)
    {
        // 从 MPMC 队列非阻塞取帧  
        if (in_queue_->try_dequeue(frame))  //阻塞的话可能会导致整个线程停住，无法响应停止信号
        {
            process_frame(worker_ctx, frame);
        }
        else
        {
            if (!running_) break;
            // 队列空，短暂休眠1ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 线程退出前，清空队列中   剩余帧
    printf("[Worker %d] Draining remaining frames...\n", worker_id);
    while (in_queue_->try_dequeue(frame))   
    {
        // 直接释放，不再推理
        if (frame->img.virt_addr) {
            free(frame->img.virt_addr);
        }
        delete frame;
    }
    
    printf("[Worker %d] Stopped\n", worker_id);
}

void WorkerPool::process_frame(worker_context_t* worker_ctx, Frame* frame)
{
    if (!frame || !frame->img.virt_addr) {
        if (frame) delete frame;
        return;
    }
    
    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));
    
    // ===== 1. 执行推理 =====
    int ret = yolov5_infer(worker_ctx, &frame->img, &od_results);
    
    if (ret != 0) {
        printf("[Worker] Inference failed for stream %d\n", frame->stream_id);
        // 即使推理失败，也要释放帧
        if (frame->img.virt_addr) {
            free(frame->img.virt_addr);
        }
        delete frame;
        return;
    }
    
    // ===== 2. 在图像上画框 =====
    if (od_results.count > 0) {
        letterbox_t* lb = &worker_ctx->last_letter_box;
        
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result* res = &od_results.results[i];
            
            // 坐标映射：推理坐标 → 原始图像坐标
            int x = (res->box.left - lb->x_pad) / lb->scale;
            int y = (res->box.top - lb->y_pad) / lb->scale;
            int w = (res->box.right - res->box.left) / lb->scale;
            int h = (res->box.bottom - res->box.top) / lb->scale;
            
            // 边界裁剪
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > frame->img.width)  w = frame->img.width - x;
            if (y + h > frame->img.height) h = frame->img.height - y;
            
            if (w <= 0 || h <= 0) continue;
            
            // 直接在 NV12 上画框
            draw_rectangle_yuv420sp(
                frame->img.virt_addr,
                frame->img.width,
                frame->img.height,
                x, y, w, h,
                0xFFFF0000,  // 红色
                2            // 线宽
            );
        }
    }
    
    // ===== 3. 更新显示缓存 =====
    int stream_id = frame->stream_id;
    if (stream_id >= 0 && stream_id < channel_count_) {
        ChannelContext* ch = &channels_[stream_id];
        g_mutex_lock(&ch->frame_lock);
        
        // 如果显示缓存未分配或大小不匹配，重新分配
        if (!ch->frame_buf.virt_addr || ch->frame_buf.size != frame->img.size) {
            if (ch->frame_buf.virt_addr) {
                free(ch->frame_buf.virt_addr);
            }
            ch->frame_buf.virt_addr = (uint8_t*)malloc(frame->img.size);
            ch->frame_buf.size = frame->img.size;
        }
        
        if (ch->frame_buf.virt_addr) {
            memcpy(ch->frame_buf.virt_addr, frame->img.virt_addr, frame->img.size);
            ch->frame_buf.width = frame->img.width;
            ch->frame_buf.height = frame->img.height;
            ch->frame_buf.format = IMAGE_FORMAT_YUV420SP_NV12;
        }
        
        g_mutex_unlock(&ch->frame_lock);
    }
    
    // ===== 4. 释放帧内存 =====
    if (frame->img.virt_addr) {
        free(frame->img.virt_addr);
    }
    delete frame;
}