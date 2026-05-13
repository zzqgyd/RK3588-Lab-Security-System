#ifndef QUEUE_MANAGER_HPP
#define QUEUE_MANAGER_HPP

#include "ring_buffer.hpp"
#include "config.h"

/**
 * @brief 多路流队列管理器
 * 
 * 每路流拥有独立的 RingBuffer
 * RingBuffer 存储 FramePtr (std::shared_ptr<Frame>)
 * 
 * RingBuffer 特性：
 * - 固定容量（RING_SIZE = 4）
 * - 写满时覆盖最旧帧（而非丢弃新帧）
 * 
 * 适用场景：多路实时监控
 * - 解码器快速写入，推理线程按需读取
 * - 推理处理不过来时，自动丢弃旧帧，保证处理的都是最新帧
 */
// 每路缓冲区容量  必须是2的幂 用位运算 & (CAP-1) 代替取模 % CAP

class QueueManager
{
public:
    QueueManager() = default;

    /**
     * @brief 获取指定流的 RingBuffer
     * @param stream_id 流 ID [0, MAX_CHANNEL-1]
     * @return RingBuffer 引用
     */
    RingBuffer<FramePtr, RING_SIZE>& get_queue(int stream_id)
    {
        return queues_[stream_id];
    }

private:
    // 每路流独立的 RingBuffer
    RingBuffer<FramePtr, RING_SIZE> queues_[MAX_CHANNEL];
};

#endif