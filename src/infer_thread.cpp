#include "infer_thread.hpp"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"

/**
 * @brief 构造函数
 * @param qm 队列管理器指针（外部传入，不持有所有权）
 * @param stream_num 流路数
 * @param yolo_ctx_array YOLO上下文数组（每路一个，外部传入）
 */
InferThread::InferThread(QueueManager* qm, int stream_num, rknn_app_context_t* yolo_ctx_array)
    : qm_(qm)
    , stream_num_(stream_num)
    , yolo_ctx_(yolo_ctx_array)
    , running_(false)
{
}

/**
 * @brief 析构函数：确保线程已停止
 */
InferThread::~InferThread()
{
    stop();
}

/**
 * @brief 启动推理线程
 */
void InferThread::start()
{
    if (running_) return;
    running_ = true;
    th_ = std::thread(&InferThread::loop, this);
    printf("[InferThread] Started, handling %d streams\n", stream_num_);
}

/**
 * @brief 停止推理线程，阻塞等待退出
 */
void InferThread::stop()
{
    if (!running_) return;
    running_ = false;
    if (th_.joinable())
    {
        th_.join();
    }
    printf("[InferThread] Stopped\n");
}

/**
 * @brief 线程主循环
 * 
 * 轮询策略：
 * - 顺序检查 stream_0 → stream_1 → ... → stream_N-1
 * - 每轮最多处理每路的一帧，防止某路饿死其他路
 */
void InferThread::loop()
{
    while (running_)
    {
        bool got_any = false;
        
        for (int i = 0; i < stream_num_; i++)
        {
            Frame* frame = nullptr;

            // 尝试从第 i 路队列取数据
            if (qm_->get_queue(i).pop(frame))
            {
                process(frame);
                got_any = true;
            }
        }

        // 本轮没有处理任何数据，短暂休眠避免 CPU 100%
        if (!got_any)
        {
            usleep(1000);  // 1ms
        }
    }

    // 线程退出前清空队列剩余数据
    drain_queues();
}

/**
 * @brief 处理单帧数据
 * @param frame 从队列取出的帧（已获得所有权）
 */
void InferThread::process(Frame* frame)
{
    if (!frame) return;

    // ===== 1. 参数校验 =====
    if (!frame->img.virt_addr || frame->img.size <= 0)
    {
        printf("[InferThread] Invalid frame, stream=%d\n", frame->stream_id);
        delete frame;       //这里是decoder new出来的数据处理！！！！！！！！
        return;
    }

    // ===== 2. 获取对应流的 YOLO 上下文 =====
    rknn_app_context_t* ctx = &yolo_ctx_[frame->stream_id];

    // ===== 3. 调用推理接口 =====
    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));
    
    int ret = yolov5_infer(ctx, &frame->img, &od_results);
    
    if (ret != 0)
    {
        printf("[InferThread] Inference failed, stream=%d, ret=%d\n", frame->stream_id, ret);
    }
    else
    {
        // ===== 4. 画框（推理结果可视化）=====
        for (int i = 0; i < od_results.count; i++)
        {
            object_detect_result* res = &od_results.results[i];
            int x = res->box.left;
            int y = res->box.top;
            int w = res->box.right - x;
            int h = res->box.bottom - y;
            
            draw_rectangle(&frame->img, x, y, w, h, COLOR_RED, 2);
        }
        // ===== 画完框后，更新显示缓存 =====!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        ChannelContext* ch = &g_cfg.channels[frame->stream_id];
        
        g_mutex_lock(&ch->frame_lock);
        
        if (ch->frame_buf.virt_addr && ch->frame_buf.size >= frame->img.size)
        {
            memcpy(ch->frame_buf.virt_addr, frame->img.virt_addr, frame->img.size);
            ch->frame_buf.width = frame->img.width;
            ch->frame_buf.height = frame->img.height;
            ch->frame_buf.format = frame->img.format;
        }
        
        g_mutex_unlock(&ch->frame_lock);
    }

    // ===== 5. 释放资源（临时方案：直接 free/delete，后续改为内存池）=====！！！！！！！！！！！
    if (frame->img.virt_addr)
    {
        free(frame->img.virt_addr);
        frame->img.virt_addr = nullptr;
    }
    delete frame;
}

/**
 * @brief 线程退出时清空队列中的剩余帧避免内存泄漏
 */
void InferThread::drain_queues()
{
    printf("[InferThread] Draining remaining frames...\n");
    int total_drained = 0;
    
    for (int i = 0; i < stream_num_; i++)
    {
        Frame* frame = nullptr;
        while (qm_->get_queue(i).pop(frame))
        {
            if (frame)
            {
                if (frame->img.virt_addr)
                {
                    free(frame->img.virt_addr);
                }
                delete frame;
                total_drained++;
            }
        }
    }
    
    printf("[InferThread] Draining done, %d frames discarded\n", total_drained);
}