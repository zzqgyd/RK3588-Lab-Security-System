// ring_buffer.hpp
#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <atomic>
#include <cstddef>
#include <utility>

/**
 * @brief 单生产者单消费者环形缓冲区（覆盖模式）
 * 
 * 核心特性：
 * - 固定容量，无动态内存分配
 * - 写满时覆盖最旧的数据（而非丢弃新数据）
 * - 读取时严格遵循 FIFO 顺序（先写入的先读）
 * - 线程安全（单生产者 + 单消费者，无锁实现）
 * 
 * 适用场景：
 * - 视频流缓冲（卡顿时丢旧帧，保证实时性）
 * - 传感器数据采集（旧数据无意义，新数据更重要）
 * - 监控画面队列（消费者慢时自动跳帧）
 * 
 * @tparam T 存储的数据类型（建议使用 std::shared_ptr 管理生命周期）
 * @tparam CAPACITY 缓冲区容量，必须是 2 的幂（便于位运算取模优化）
 */
template<typename T, size_t CAPACITY>
class RingBuffer {
private:
    // 编译期确保容量是 2 的幂
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, 
                  "CAPACITY must be power of 2");
    
    // 取模掩码：x & MASK 等价于 x % CAPACITY
    static constexpr size_t MASK = CAPACITY - 1;

public:
    RingBuffer() 
        : write_idx_(0)
        , read_idx_(0)
    {
        // 初始化为 nullptr（假设 T 是 shared_ptr 之类可为空的类型）
        for (size_t i = 0; i < CAPACITY; ++i) {
            buffer_[i] = nullptr;
        }
    }
    
    ~RingBuffer() = default;
    
    // 禁止拷贝和赋值
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    // 支持移动（可选）
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief 写入数据（生产者调用）
     * 
     * 行为：
     * - 如果缓冲区未满：正常写入队尾
     * - 如果缓冲区已满：覆盖最旧的数据（读指针前移），新数据写入队尾
     * 
     * 线程安全：仅支持单生产者，多生产者需外部加锁
     * 
     * @param item 要写入的数据（使用移动语义）
     */
    void write(T item) {
        // 获取当前读写指针
        size_t write = write_idx_.load(std::memory_order_relaxed);
        size_t read = read_idx_.load(std::memory_order_acquire);
        
        // 计算当前缓冲区中的元素数量
        size_t count = (write - read) & MASK;
        
        if (count == CAPACITY) {
            // ===== 缓冲区已满：覆盖最旧的数据 =====
            // 策略：将读指针向前移动一格
            // 效果：最旧的那帧被"逻辑删除"（不在可读范围内）
            size_t new_read = (read + 1) & MASK;
            read_idx_.store(new_read, std::memory_order_release);
            
            // 更新 read 用于后续（可选，不影响正确性）
            read = new_read;
        }
        
        // 写入新数据到写指针位置
        buffer_[write] = std::move(item);
        
        // 更新写指针（release 语义确保数据写入完成后再更新指针）
        size_t new_write = (write + 1) & MASK;
        write_idx_.store(new_write, std::memory_order_release);
    }
    
    /**
     * @brief 读取数据（消费者调用）
     * 
     * 行为：
     * - 严格遵循 FIFO 顺序：先写入的先被读出
     * - 如果缓冲区为空，返回 nullptr
     * - 非阻塞，立即返回
     * 
     * 线程安全：仅支持单消费者，多消费者需外部加锁
     * 
     * @return 读取的数据，如果为空返回 nullptr
     */
    T read() {
        // 获取当前读写指针
        size_t write = write_idx_.load(std::memory_order_acquire);
        size_t read = read_idx_.load(std::memory_order_relaxed);
        
        // 判断是否为空
        if (read == write) {
            return nullptr;
        }
        
        // 取出读指针位置的数据
        T result = std::move(buffer_[read]);
        
        // 更新读指针（release 语义确保数据读取完成后再更新指针）
        size_t new_read = (read + 1) & MASK;
        read_idx_.store(new_read, std::memory_order_release);
        
        return result;
    }
    
    /**
     * @brief 获取当前缓冲区中的元素数量
     * 
     * 注意：在并发环境下，返回值是近似值
     * （返回时可能已经被生产者/消费者修改）
     * 
     * @return 当前元素数量（0 ~ CAPACITY）
     */
    size_t size() const {
        size_t write = write_idx_.load(std::memory_order_relaxed);
        size_t read = read_idx_.load(std::memory_order_relaxed);
        return (write - read) & MASK;
    }
    
    /**
     * @brief 判断缓冲区是否为空
     * 
     * @return true 为空，false 非空
     */
    bool empty() const {
        return size() == 0;
    }
    
    /**
     * @brief 判断缓冲区是否已满
     * 
     * @return true 已满，false 未满
     */
    bool full() const {
        return size() == CAPACITY;
    }
    
    /**
     * @brief 获取缓冲区容量
     * 
     * @return 容量（编译期常量）
     */
    constexpr size_t capacity() const {
        return CAPACITY;
    }
    
    /**
     * @brief 清空缓冲区（消费者调用）
     * 
     * 将读指针直接移动到写指针位置，逻辑清空所有数据
     * 实际数据会随着 shared_ptr 的析构自动释放
     */
    void clear() {
        size_t write = write_idx_.load(std::memory_order_acquire);
        size_t read = read_idx_.load(std::memory_order_relaxed);
        
        // 逐个释放中间的 shared_ptr
        while (read != write) {
            buffer_[read] = nullptr;
            read = (read + 1) & MASK;
        }
        
        // 读指针跳到写指针位置
        read_idx_.store(write, std::memory_order_release);
    }

private:
    // ===== 数据成员 =====
    
    // 缓冲区数组（使用原始数组，元素由 shared_ptr 管理生命周期）
    T buffer_[CAPACITY];
    
    // 写指针（生产者更新）
    // alignas(64) 对齐到缓存行，避免 false sharing
    alignas(64) std::atomic<size_t> write_idx_;
    
    // 读指针（消费者更新）
    alignas(64) std::atomic<size_t> read_idx_;
};

#endif // RING_BUFFER_HPP