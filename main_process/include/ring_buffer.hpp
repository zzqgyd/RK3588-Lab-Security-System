#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <atomic>
#include <cstddef>
#include <utility>

/**
 * @brief 单生产者单消费者环形缓冲区（覆盖模式）- 计数器派实现
 * 
 * 核心特性：
 * - 固定容量，无动态内存分配
 * - 写满时覆盖最旧的数据（而非丢弃新数据）
 * - 读取时严格遵循 FIFO 顺序（先写入的先读）
 * - 线程安全（单生产者 + 单消费者，无锁实现）
 * 
 * 实现原理（计数器派）：
 * - write_count_ 和 read_count_ 是无限增长的逻辑计数器（永不回绕）
 * - 物理数组下标 = 逻辑计数器 & MASK（位运算取模）
 * - 元素数量 = write_count_ - read_count_（直接减法，不需要 & MASK）
 * - 满判断 = write_count_ - read_count_ == CAPACITY
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
    static_assert(CAPACITY > 0 && (CAPACITY & (CAPACITY - 1)) == 0,
                  "CAPACITY must be a positive power of 2");
    
    // 取模掩码：x & MASK 等价于 x % CAPACITY
    static constexpr size_t MASK = CAPACITY - 1;

public:
    RingBuffer() 
        : write_count_(0)
        , read_count_(0)
    {
        // 初始化为 nullptr（假设 T 是 shared_ptr 之类可为空的类型）
        for (size_t i = 0; i < CAPACITY; ++i) {
            buffer_[i] = nullptr;
        }
    }
    
    ~RingBuffer() = default;
    
    // 禁止拷贝和赋值（因为有原子操作数据）
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    // 禁止移动（用的是原始数组而不是指针，移动硬搬太慢）
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief 写入数据（生产者调用）
     * 行为：
     * - 如果缓冲区未满：正常写入队尾
     * - 如果缓冲区已满：覆盖最旧的数据（读指针前移），新数据写入队尾
     * 
     * 线程安全：仅支持单生产者，多生产者需外部加锁
     * @param item 要写入的数据（使用移动语义）
     */
    void write(T item) {
        // ========== 1. 获取当前读写计数 ==========
        // 生产者自己写 write_count_，用 memory_order_relaxed 最轻量
        size_t write = write_count_.load(std::memory_order_relaxed);
        // 消费者读之后会更新 read_count_，acquire 保证生产者看到的是最新的 read_count_
        size_t read = read_count_.load(std::memory_order_acquire);
        
        // ========== 2. 判断是否需要覆盖 ==========
        // 关键：直接相减，不需要 & MASK！
        // 因为 write_count_ 和 read_count_ 永远不回绕
        if (write - read == CAPACITY) {
            // 缓冲区已满：覆盖最旧的数据
            // 策略：将读计数向前移动一格
            // 效果：最旧的那帧被"逻辑删除"（不在可读范围内）
            read = read + 1;  // 先更新局部变量
            // release 确保上面的数据写入完成后再更新全局计数
            read_count_.store(read, std::memory_order_release);
        }
        
        // ========== 3. 写入数据 ==========
        // 关键：取模在这里做，而不是存在 write 里
        // write 是无限增长的逻辑计数，& MASK 得到物理数组下标
        buffer_[write & MASK] = std::move(item);
        
        // ========== 4. 更新写计数 ==========
        // release 语义确保数据写入完成后再更新计数
        // 注意：write_count_ 永远递增，不回绕！
        write_count_.store(write + 1, std::memory_order_release);
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
        // ========== 1. 获取当前读写计数 ==========
        // acquire 确保读到生产者最新的 write_count_
        size_t write = write_count_.load(std::memory_order_acquire);
        // 消费者自己读 read_count_，用 memory_order_relaxed 最轻量
        size_t read = read_count_.load(std::memory_order_relaxed);
        
        // ========== 2. 判断是否为空 ==========
        // 直接比较计数，不需要 & MASK
        if (read == write) {
            return nullptr;  // 缓冲区为空
        }
        
        // ========== 3. 读取数据 ==========
        // read 是逻辑计数，& MASK 得到物理数组下标
        T result = std::move(buffer_[read & MASK]);
        
        // ========== 4. 更新读计数 ==========
        // release 语义确保数据读取完成后再更新计数
        // 注意：read_count_ 永远递增，不回绕！
        read_count_.store(read + 1, std::memory_order_release);
        
        return result;
    }
    
    /**
     * @brief 获取当前缓冲区中的元素数量
     * 
     * 注意：在并发环境下，返回值是近似值
     * （返回时可能已经被生产者/消费者修改）
     * 
     * 关键：直接相减，不需要 & MASK
     * 
     * @return 当前元素数量（0 ~ CAPACITY）
     */
    size_t size() const {
        size_t write = write_count_.load(std::memory_order_relaxed);
        size_t read = read_count_.load(std::memory_order_relaxed);
        return write - read;  // 直接相减，绝对正确
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
     * 将读计数直接移动到写计数位置，逻辑清空所有数据
     * 实际数据会随着 shared_ptr 的析构自动释放
     */
    void clear() {
        size_t write = write_count_.load(std::memory_order_acquire);
        size_t read = read_count_.load(std::memory_order_relaxed);
        
        // 逐个释放中间的 shared_ptr
        // 注意：这里用物理下标循环，因为 buffer_ 是物理数组
        while (read != write) {
            buffer_[read & MASK] = nullptr;
            read = read + 1;  // 逻辑计数递增
        }
        
        // 读计数跳到写计数位置
        read_count_.store(write, std::memory_order_release);
    }

private:
    // ===== 数据成员 =====
    /**
     * 缓冲区数组（使用原始数组，元素由 shared_ptr 管理生命周期）
     */
    T buffer_[CAPACITY];
    
    /**
     * 关于伪共享（false sharing）的说明：
     * CPU 缓存是以缓存行（64字节）为单位读写的。
     * 如果不做对齐：
     * ┌─────────────────────────────────────────────────────────────┐
     * │                      缓存行（64字节）                        │
     * │  ┌──────────────────┐  ┌──────────────────┐               │
     * │  │  write_count_    │  │   read_count_    │               │
     * │  │  (生产者修改)     │  │   (消费者修改)    │               │
     * │  └──────────────────┘  └──────────────────┘               │
     * └─────────────────────────────────────────────────────────────┘
     * 问题：
     * - 生产者 CPU 核心修改 write_count_ → 整个缓存行被标记为"脏"
     * - 消费者 CPU 核心的整个缓存行失效
     * - 消费者下次读 read_count_ 时，缓存未命中，要从内存重新加载
     * - 反之亦然，两个核心互相拖慢
     * 
     * 解决方案：alignas(64) 强制两个变量在不同缓存行
     * 
     * ┌──────────────────────┐  ┌──────────────────────┐
     * │    缓存行（64字节）   │  │    缓存行（64字节）   │
     * │  ┌────────────────┐  │  │  ┌────────────────┐  │
     * │  │  write_count_  │  │  │  │  read_count_   │  │
     * │  └────────────────┘  │  │  └────────────────┘  │
     * └──────────────────────┘  └──────────────────────┘
     *     生产者独占修改              消费者独占修改
     * 结果：两个核心互不影响，性能提升。
     */
    
    /**
     * @brief 写计数（生产者更新）
     * 
     * 这是一个无限增长的逻辑计数器，永远不会回绕。
     * - 每次写入后 +1
     * - 物理数组下标 = write_count_ & MASK
     * - 取值范围：0 ~ 2^64-1（约 58 万年才会溢出，假设每秒 100 万次写入）
     */
    alignas(64) std::atomic<size_t> write_count_;
    
    /**
     * @brief 读计数（消费者更新）
     * 
     * 这是一个无限增长的逻辑计数器，永远不会回绕。
     * - 每次读取后 +1
     * - 物理数组下标 = read_count_ & MASK
     * - 取值范围：0 ~ 2^64-1
     * 
     * 关键关系：
     * - 元素数量 = write_count_ - read_count_（永远 <= CAPACITY）
     * - 为空：write_count_ == read_count_
     * - 为满：write_count_ - read_count_ == CAPACITY
     */
    alignas(64) std::atomic<size_t> read_count_;
};

#endif // RING_BUFFER_HPP