#ifndef WORKER_POOL_HPP
#define WORKER_POOL_HPP

#include "mpmc_queue.hpp"
#include "yolov5_infer.h"
#include "drawing.h"
#include "scheduler.hpp"
#include "config.h"
#include <vector>
#include <thread>
#include <atomic>

/**
 * @brief Worker 线程池
 * 
 * 职责：
 * 1. 从 MPMC 队列消费 FramePtr
 * 2. 调用 YOLO 推理
 * 3. 在图像上画框
 * 4. 更新显示缓存
 * 5. 释放 Frame 内存
 * 
 * 线程模型：3 个 Worker 线程
 * 
 * 所有权说明：
 * - 不持有 MPMCQueue（外部传入）
 * - 持有 worker_ctxs_（内部分配）
 */
class WorkerPool {
public:
    /**
     * @brief 构造函数
     * @param num_workers    Worker 数量
     * @param in_queue       输入 MPMC 队列
     * @param worker_ctxs    Worker 上下文数组（外部传入，已初始化）
     * @param channels       通道上下文数组（用于更新显示缓存）
     * @param channel_count  通道数量
     */
    WorkerPool(int num_workers, 
               MPMCQueue<FramePtr>* in_queue,
               worker_context_t* worker_ctxs,
               ChannelContext* channels,
               int channel_count);
    
    ~WorkerPool();
    
    /**
     * @brief 启动所有 Worker 线程
     */
    void start();
    
    /**
     * @brief 停止所有 Worker 线程
     */
    void stop();
    
private:
    /**
     * @brief Worker 线程主循环
     * @param worker_id Worker ID [0, num_workers-1]
     */
    void worker_loop(int worker_id);
    
    /**
     * @brief 处理单帧
     * @param worker_ctx Worker 上下文
     * @param frame      待处理的帧
     */
    void process_frame(worker_context_t* worker_ctx, FramePtr frame);
    
private:
    int num_workers_;                       // Worker 数量
    MPMCQueue<FramePtr>* in_queue_;           // 输入队列
    worker_context_t* worker_ctxs_;         // Worker 上下文数组（外部传入）
    ChannelContext* channels_;              // 通道上下文数组
    int channel_count_;                     // 通道数量
    
    std::vector<std::thread> threads_;      // 工作线程
    std::atomic<bool> running_;             // 运行标志
};

#endif // WORKER_POOL_HPP