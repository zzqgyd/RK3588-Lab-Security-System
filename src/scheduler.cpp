#include "scheduler.hpp"
#include <unistd.h>
#include <stdio.h>

Scheduler::Scheduler(QueueManager* qm, MPMCQueue<Frame*>* out_queue, int stream_num)
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

bool Scheduler::should_skip(int stream_id)
{
    // 第一阶段：简单策略，不跳过任何帧
    // 后续阶段会增加智能跳帧!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    (void)stream_id;
    return false;
}

void Scheduler::loop()
{
    Frame* frame = nullptr;
    
    while (running_)
    {
        bool got_any = false;
        
        // 轮询所有 SPSC 队列!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        for (int i = 0; i < stream_num_; i++)
        {
            SPSCQueue<Frame*, QUEUE_SIZE>& q = qm_->get_queue(i);
            
            // 尝试取出一帧（非阻塞）有数据立即返回 true，没数据立即返回 false
            if (q.pop(frame))   
            {
                // 更新状态
                stream_states_[i].last_pop_time = std::chrono::steady_clock::now();
                
                // 跳帧判断（第一阶段：不跳过）!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                if (should_skip(i))
                {
                    // 释放帧内存
                    if (frame->img.virt_addr) {
                        free(frame->img.virt_addr);
                    }
                    delete frame;
                    continue;
                }
                
                // 投递到 MPMC 队列（阻塞直到成功） !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                // 非阻塞的话是自旋+sleep频繁唤醒CPU，效率极低，且不利于背压传导（1）
                // 阻塞时，整个循环暂停，公平调度（1）
                // Worker慢 → MPMC满 → Scheduler阻塞 → 不再从SPSC取帧 → SPSC积压 → 采集端感知到背压（2）
                out_queue_->enqueue(frame); 
                got_any = true;
            }
        }
        
        // 本轮没有处理任何数据，短暂休眠,防止CPU空转
        if (!got_any)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 线程退出前，清空所有 SPSC 队列中的     剩余帧!!!
    printf("[Scheduler] Draining remaining frames...\n");
    for (int i = 0; i < stream_num_; i++)
    {
        SPSCQueue<Frame*, QUEUE_SIZE>& q = qm_->get_queue(i);
        while (q.pop(frame))
        {
            if (frame->img.virt_addr) {
                free(frame->img.virt_addr);
            }
            delete frame;
        }
    }
}