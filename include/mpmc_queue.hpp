#ifndef MPMC_QUEUE_HPP
#define MPMC_QUEUE_HPP

#include "rigtorp_mpmc.hpp"  // 你提供的开源 MPMC 队列

/**
 * @brief MPMC 无锁队列封装（基于 rigtorp::MPMCQueue）
 * 
 * 特点：
 * - 多生产者多消费者无锁
 * - 固定容量（构造时指定）
 * - 线程安全
 * - 单头文件，无额外依赖
 */
template<typename T>
class MPMCQueue {
public:
    /**
     * @brief 构造函数
     * @param capacity 队列容量（必须 >= 1）
     */
    explicit MPMCQueue(size_t capacity) 
        : queue_(capacity) {}
    
    /**
     * @brief 入队（阻塞直到成功）
     * @param item 要入队的元素（拷贝）
     */
    void enqueue(const T& item) {
        queue_.push(item);
    }
    
    /**
     * @brief 入队（移动语义）
     */
    void enqueue(T&& item) {
        queue_.push(std::move(item));
    }
    
    /**
     * @brief 尝试入队（非阻塞）
     * @param item 要入队的元素
     * @return true 成功，false 队列满
     */
    bool try_enqueue(const T& item) {
        return queue_.try_push(item);
    }
    
    /**
     * @brief 尝试入队（移动语义，非阻塞）
     */
    bool try_enqueue(T&& item) {
        return queue_.try_push(std::move(item));
    }
    
    /**
     * @brief 出队（阻塞直到有数据）
     * @param item [OUT] 出队的元素
     */
    void dequeue(T& item) {
        queue_.pop(item);
    }
    
    /**
     * @brief 尝试出队（非阻塞）
     * @param item [OUT] 出队的元素
     * @return true 成功，false 队列空
     */
    bool try_dequeue(T& item) {
        return queue_.try_pop(item);
    }
    
    /**
     * @brief 获取队列中大约的元素数量
     * @return 元素数量（可能为负表示有等待的消费者）
     */
    ptrdiff_t size() const {
        return queue_.size();
    }
    
    /**
     * @brief 判断队列是否为空
     * @return true 队列空
     */
    bool empty() const {
        return queue_.empty();
    }
    
private:
    rigtorp::MPMCQueue<T> queue_;
};

#endif // MPMC_QUEUE_HPP