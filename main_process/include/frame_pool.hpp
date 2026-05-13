#ifndef FRAME_POOL_HPP
#define FRAME_POOL_HPP

#include "config.h"
#include "memory_pool.hpp"
#include <memory>

/**
 * @brief Frame 对象内存池
 * 
 * 职责：
 * - 管理 Frame 对象的内存分配和回收
 * - 与 std::shared_ptr 集成，通过自定义删除器自动归还
 * - 每路流独立一个 FramePool 实例
 * 
 * 使用方式：
 *   FramePool pool(4);  // 预分配 4 个 Frame 的容量
 *   FramePtr frame = pool.acquire();
 *   // 使用 frame...
 *   // 引用计数归零时，Frame 自动析构并归还池中
 */
class FramePool {
public:
    /**
     * @param capacity 池容量（Frame 对象数量） 即小块数量
     */
    explicit FramePool(std::size_t capacity = 4)
        : pool_(sizeof(Frame), capacity)
    {
    }

    ~FramePool() = default;

    /**
     * @brief 从池中获取一个 Frame
     * @return FramePtr，如果池已空且无法扩展则返回 nullptr
     */
    FramePtr acquire() {
        void* ptr = pool_.allocate();
        if (!ptr) {
            return nullptr;
        }
        
        // placement new：在已分配的内存上构造 Frame
        Frame* frame = new(ptr) Frame();
        
        // 创建 shared_ptr，自定义删除器：析构 Frame 并归还内存
        // shared_ptr(T* ptr, Deleter deleter);
        // delete 会同时调用析构函数 和 释放内存。但我们的内存是从池里拿的，不能 delete
        return FramePtr(frame, [this](Frame* f) {
            f->~Frame();               // ① 只析构，不释放内存
            pool_.deallocate(f);       // ② 内存归还到池中
        });
    }

    /**
     * @brief 获取池容量（每页小块数）
     */
    std::size_t capacity() const {
        return pool_.blocks_per_page();
    }

    // 禁止拷贝
    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

private:
    FixedSizePool pool_;
};

#endif // FRAME_POOL_HPP