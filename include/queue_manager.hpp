#ifndef QUEUE_MANAGER_HPP
#define QUEUE_MANAGER_HPP

#include "spsc_queue.hpp"
#include "config.h"

/**
 * @brief 多路流队列管理器
 * 
 * 每路流拥有独立的 SPSC 队列，互不干扰
 * 队列存储 Frame* 实现零拷贝传递
 * 
 * 所有权规则：
 * - 生产者分配 Frame*，push 后转移所有权
 * - 消费者 pop 后获得所有权，用完后归还内存池
 * - 队列不负责释放内存
 */
#define MAX_STREAM 4      // 最大流路数
#define QUEUE_SIZE 8      // 每路队列容量（实际存 7 个）

class QueueManager
{
public:
    QueueManager() = default;

    /**
     * @brief 获取指定流的队列      Frame*指针配合内存池实现全链路零拷贝
     * @param stream_id 流 ID [0, MAX_STREAM-1]
     * @return &：只返回一个指针大小的引用，零开销  
     */
    SPSCQueue<Frame*, QUEUE_SIZE>& get_queue(int stream_id)
    {
        return queues_[stream_id];
    }

private:
    // 模板实例
    // 数组而非 vector：编译期定长，内存连续，无堆分配
    SPSCQueue<Frame*, QUEUE_SIZE> queues_[MAX_STREAM];
};

#endif