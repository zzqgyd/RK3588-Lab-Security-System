#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <vector>
#include <mutex>
#include <cstddef>

/**
 * 向上对齐函数
 * 将 size 向上对齐到 align 的整数倍
 * @param size 需要对齐的大小
 * @param align 对齐边界（必须是2的幂，如 4, 8, 16）
 * @return 对齐后的大小
 * 示例：align_up(5, 8) = 8, align_up(8, 8) = 8, align_up(9, 8) = 16
 */
static inline std::size_t align_up(std::size_t size, std::size_t align) {
    return (size + (align - 1)) & ~(align - 1);
}

/**
 * @brief 固定大小内存池（线程安全版）
 * 
 * 设计思路：
 * - 预先申请大块内存（页），切分成固定大小的小块
 * - 用自由链表管理空闲小块
 * - 分配/释放 O(1) 时间复杂度
 * - 线程安全：内部用 std::mutex 保护
 */
class FixedSizePool {
public:
    /**
     * @param block_size 每个小块的大小（字节）
     * @param blocks_per_page 每页包含的小块数量，默认 16
     */
    explicit FixedSizePool(std::size_t block_size, std::size_t blocks_per_page = 16)
        : blocks_per_page_(blocks_per_page)
        , free_list_(nullptr)
    {
        block_size_ = adjust_block_size(block_size);
    }

    ~FixedSizePool() {
        // 释放所有申请的大块内存
        // 使用 ::operator delete[] 而不是 delete[]，避免调用析构函数
        for (auto page : pages_) {
            ::operator delete[](page);
        }
    }

    /**
     * @brief 分配一个小块内存
     * @return 内存指针，失败返回 nullptr
     * 时间复杂度：O(1)
     */
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 空闲链表为空，向系统申请新的一页
        if (free_list_ == nullptr) {
            expand();
            if (free_list_ == nullptr) {
                return nullptr;  // 内存不足
            }
        }
        
        // 从链表头部取出一个节点
        Node* node = free_list_;
        free_list_ = node->next;
        return node;
    }

    /**
     * @brief 归还小块内存
     * @param ptr 必须是由本池 allocate 返回的指针
     * 时间复杂度：O(1)
     */
    void deallocate(void* ptr) {
        if (ptr == nullptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 归还节点到链表头部
        Node* node = static_cast<Node*>(ptr);
        node->next = free_list_;
        free_list_ = node;
    }

    std::size_t get_block_size() const { return block_size_; }
    std::size_t blocks_per_page() const { return blocks_per_page_; }

    // 禁止拷贝
    FixedSizePool(const FixedSizePool&) = delete;
    FixedSizePool& operator=(const FixedSizePool&) = delete;

private:
    /**
     * @brief 向系统申请新的一页，切分成小块并串入空闲链表
     */
    void expand() {
        std::size_t page_size = blocks_per_page_ * block_size_;
        char* page = static_cast<char*>(::operator new[](page_size));
        pages_.push_back(page);

        // 将整页切分成小块，串入自由链表
        for (std::size_t i = 0; i < blocks_per_page_; ++i) {
            char* addr = page + i * block_size_;
            Node* node = reinterpret_cast<Node*>(addr);
            node->next = free_list_;
            free_list_ = node;
        }
    }

    /**
     * @brief 调整块大小
     * - 块大小至少能存下一个指针（用于链表）
     * - 对齐到指针对齐要求
     */
    std::size_t adjust_block_size(std::size_t block_size) {
        std::size_t min_size = sizeof(void*);
        std::size_t raw_size = block_size < min_size ? min_size : block_size;
        return align_up(raw_size, alignof(void*));
    }

    std::size_t block_size_;          // 每个小块的大小（字节）
    std::size_t blocks_per_page_;     // 每页包含的小块数量
    
    struct Node {
        Node* next;                   // 指向下一个空闲块
    };
    
    std::vector<void*> pages_;        // 所有申请的大块内存指针
    Node* free_list_;                 // 空闲链表头指针
    std::mutex mutex_;                // 线程安全锁
};

#endif // MEMORY_POOL_HPP