#include "scheduler.hpp"
#include <unistd.h>
#include <stdio.h>

Scheduler::Scheduler(QueueManager* qm, MPMCQueue<FramePtr>* out_queue, int stream_num)
    : qm_(qm)
    , out_queue_(out_queue)
    , stream_num_(stream_num)
    , running_(false)
{
    stream_states_.resize(stream_num);
    for (int i = 0; i < stream_num; i++) {
        stream_states_[i].last_processed_seq = 0;
        stream_states_[i].last_pop_time = std::chrono::steady_clock::now();
    }
}

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::start()
{
    if (running_) return;
    running_ = true;
    th_ = std::thread(&Scheduler::loop, this);
    printf("[Scheduler] Started, handling %d streams\n", stream_num_);
}

void Scheduler::stop()
{
    if (!running_) return;
    running_ = false;
    if (th_.joinable()) {
        th_.join();
    }
    printf("[Scheduler] Stopped\n");
}

void Scheduler::loop()
{
    FramePtr frame = nullptr;
    
    while (running_)
    {
        bool got_any = false;
        
        // 轮询所有 RingBuffer
        for (int i = 0; i < stream_num_; i++)
        {
            RingBuffer<FramePtr, RING_SIZE>& rb = qm_->get_queue(i);
            
            // 读取一帧（FIFO 顺序）
            frame = rb.read();
            
            if (frame)
            {
                got_any = true;
                stream_states_[i].last_pop_time = std::chrono::steady_clock::now();
                stream_states_[i].last_processed_seq = frame->seq;
                
                // 非阻塞投递到 MPMC 队列
                if (!out_queue_->try_enqueue(frame))
                {
                    stream_states_[i].mpmc_dropped++;
                    
                    static int drop_print_counter = 0;
                    if (++drop_print_counter % 100 == 0) {
                        printf("[Scheduler] MPMC queue full, dropping frame\n");
                    }
                }
            }
        }
        
        if (!got_any)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    // 清空剩余帧
    printf("[Scheduler] Draining remaining frames...\n");
    for (int i = 0; i < stream_num_; i++)
    {
        RingBuffer<FramePtr, RING_SIZE>& rb = qm_->get_queue(i);
        rb.clear();
    }
}