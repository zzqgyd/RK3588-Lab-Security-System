#ifndef SPSC_QUEUE_HPP
#define SPSC_QUEUE_HPP

#include <atomic>

/**
 * @brief 单生产者单消费者无锁队列
 *
 * 使用场景：
 * decoder线程 → infer线程
 *
 * 特点：
 * - 无锁
 * - 无malloc
 * - 固定容量（防止内存爆炸）
 */
template<typename T, int SIZE>
class SPSCQueue
{
public:
    SPSCQueue()
    {
        head_.store(0);
        tail_.store(0);
    }

    /**
     * @brief 入队（生产者调用）
     */
    bool push(const T& item) {
        // 1. 看自己写到哪了 (relaxed 仅需当前值)
        int head = head_.load(std::memory_order_relaxed);
        int next = (head + 1) % SIZE;

        // 2. 看看消费者是否堵住了路 (acquire 确保看见消费者最新的更新)
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // 队列满，丢弃数据（不阻塞）
        }

        // 3. 没人堵路，写入数据
        buffer_[head] = item;
        
        // 4. 把写指针向前推一格 (release 确保数据写完才对消费者可见)
        head_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief 出队（消费者调用）
     */
    bool pop(T& item) {
        // 1. 看自己读到哪了
        int tail = tail_.load(std::memory_order_relaxed);

        // 2. 如果读写指针相同，说明没东西可读 (acquire 确保看见生产者写入的新数据)
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // 队列空
        }

        // 3. 读数据
        item = buffer_[tail];
        
        // 4. 把读指针向前推一格 (release 确保数据读完才对生产者可见)
        tail_.store((tail + 1) % SIZE, std::memory_order_release);
        return true;
    }

private:
    T buffer_[SIZE];

    std::atomic<int> head_; //生产者拿着，指向下一个空位
    std::atomic<int> tail_; //消费者拿着，指向下一个待读数据。
};

#endif