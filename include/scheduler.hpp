#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "queue_manager.hpp"
#include "mpmc_queue.hpp"
#include "config.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

/**
 * @brief 调度器线程
 * 
 * 职责：
 * 1. 轮询所有 SPSC 队列
 * 2. 执行跳帧策略（当前阶段：简单取最新帧）
 * 3. 将有效的 Frame* 投递到 MPMC 队列
 * 
 * 线程模型：单线程
 * 
 * 所有权说明：
 * - 不持有 QueueManager（外部传入）
 * - 不持有 MPMCQueue（外部传入）
 * - 不负责释放 Frame（Worker 负责）
 */
class Scheduler {
public:
    /**
     * @brief 构造函数
     * @param qm           队列管理器（SPSC 队列来源）
     * @param out_queue    输出 MPMC 队列
     * @param stream_num   流路数
     */
    Scheduler(QueueManager* qm, MPMCQueue<Frame*>* out_queue, int stream_num);
    
    ~Scheduler();
    
    /**
     * @brief 启动调度器线程
     */
    void start();
    
    /**
     * @brief 停止调度器线程
     */
    void stop();
    
private:
    /**
     * @brief 线程主循环
     */
    void loop();
    
    /**
     * @brief 判断是否应该跳过此帧
     * @param stream_id 流 ID
     * @return true 跳过，false 处理
     */
    bool should_skip(int stream_id);
    
private:
    QueueManager* qm_;                  // SPSC 队列来源
    MPMCQueue<Frame*>* out_queue_;      // MPMC 输出队列
    int stream_num_;                    // 流路数
    
    // 每路的状态（用于跳帧）
    struct StreamState {
        uint64_t last_processed_seq = 0;    // 最后处理的帧序号
        std::chrono::steady_clock::time_point last_pop_time;  // 最后取帧时间
    };
    std::vector<StreamState> stream_states_;
    
    std::atomic<bool> running_;         // 运行标志
    std::thread th_;                    // 工作线程
};

#endif // SCHEDULER_HPP