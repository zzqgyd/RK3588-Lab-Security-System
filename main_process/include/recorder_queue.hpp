/**
 * @file recorder_queue.hpp
 * @brief 独立录像线程池 - 与推理解耦，一路一流一个录像线程
 * 
 * ================================================================
 * 架构设计
 * ================================================================
 * 
 * 输入层（Worker线程）                  录像层（Recorder线程）
 * ┌──────────────────┐              ┌──────────────────────────┐
 * │ Worker 0         │              │ Recorder 0 (stream 0)    │
 * │ Worker 1  ───┐   │              │   ├─ VideoRecorder       │
 * │ Worker 2  ───┤   │              │   └─ is_recording (atomic)│
 * │ ...         ├───┼──────────────▶│                           │
 * │ Worker 8  ───┘   │   per-stream │ Recorder 1 (stream 1)    │
 * └──────────────────┘   队列        │   ├─ VideoRecorder       │
 *                        (MPMC)      │   └─ is_recording (atomic)│
 *                                    │ ...                       │
 *                                    │ Recorder N (stream N)    │
 *                                    └──────────────────────────┘
 * 
 * 生命周期保证：
 *   1. RecorderTask 持有 FramePtr（shared_ptr）→ gst_sample 不会被释放
 *   2. gst_sample 持有 GstBuffer → dmabuf fd 有效
 *   3. 编码器完成编码后 AVBufferRef 释放 → 不再引用 fd
 *   4. RecorderTask 析构 → FramePtr 引用计数 -1 → 可能释放 GstBuffer
 * 
 * 设计原则：
 *   1. 录像线程持有 FramePtr，保证帧生命周期安全
 *   2. 原子状态管理，避免多线程竞争
 *   3. 每路流独立录像线程，避免任务分发竞争
 *   4. 非阻塞提交，推理线程永不等待
 */

#ifndef RECORDER_QUEUE_HPP
#define RECORDER_QUEUE_HPP

#include "mpmc_queue.hpp"
#include "config.h"
#include "video_recorder.h"
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <cstring>

// ================================================================
// 前向声明（解决循环依赖）
// ================================================================
struct Frame;
using FramePtr = std::shared_ptr<Frame>;

/**
 * @brief 录像任务类型
 */
enum class RecorderTaskType {
    NONE,           // 空任务
    FRAME,          // 喂帧任务（持有 FramePtr 引用计数）
    START,          // 开始录像
    STOP            // 停止录像
};

/**
 * @brief 录像任务结构体
 * 
 * 关键设计：
 *   - 持有 FramePtr（shared_ptr），确保帧 buffer 生命周期安全
 *   - 禁止拷贝，允许移动（移动后源对象类型变为 NONE）
 *   - 录像线程完成编码前，FramePtr 不会被释放
 *   - 录像线程完成后，RecorderTask 析构，FramePtr 引用计数 -1
 */
struct RecorderTask {
    RecorderTaskType type;          // 任务类型
    int stream_id;                  // 哪路流
    int device_id;                  // 哪个设备触发（用于日志）
    FramePtr frame;                 // 【关键】持有帧引用，确保 buffer 不释放
    uint64_t pts;                   // 时间戳（FRAME 任务使用）

    // 开始录像专用
    char start_time[64];

    // 停止录像专用
    char end_time[64];

    // ---- 构造函数 ----
    RecorderTask()
        : type(RecorderTaskType::NONE)
        , stream_id(0)
        , device_id(0)
        , frame(nullptr)
        , pts(0)
    {
        start_time[0] = '\0';
        end_time[0] = '\0';
    }

    // ---- 禁止拷贝（因为持有 unique 语义资源）----
    RecorderTask(const RecorderTask&) = delete;
    RecorderTask& operator=(const RecorderTask&) = delete;

    // ---- 允许移动（FramePtr 移动后源变为 nullptr）----
    RecorderTask(RecorderTask&& other) noexcept {
        *this = std::move(other);
    }

    RecorderTask& operator=(RecorderTask&& other) noexcept {
        if (this != &other) {
            type = other.type;
            stream_id = other.stream_id;
            device_id = other.device_id;
            frame = std::move(other.frame);  // shared_ptr 移动，源变为 nullptr
            pts = other.pts;
            strncpy(start_time, other.start_time, sizeof(start_time) - 1);
            start_time[sizeof(start_time) - 1] = '\0';
            strncpy(end_time, other.end_time, sizeof(end_time) - 1);
            end_time[sizeof(end_time) - 1] = '\0';

            // 重置源对象
            other.type = RecorderTaskType::NONE;
            other.stream_id = 0;
            other.device_id = 0;
            other.pts = 0;
        }
        return *this;
    }

    // ===== 工厂方法 =====

    /**
     * @brief 创建喂帧任务（移动语义版本）
     * 
     * FramePtr 移动后，调用者的 frame 变为 nullptr，
     * 录像线程持有唯一引用，保证 buffer 生命周期。
     * 
     * @param f   帧数据（shared_ptr，所有权转移给任务）
     * @param sid 流ID
     * @param p   PTS 时间戳
     */
    static RecorderTask make_frame(FramePtr& f, int sid, uint64_t p) {
        RecorderTask task;
        task.type = RecorderTaskType::FRAME;
        task.stream_id = sid;
        task.frame = std::move(f);  // 转移所有权，f 变为 nullptr
        task.pts = p;
        return task;
    }

    /**
     * @brief 创建喂帧任务（共享引用版本）
     * 
     * 与 make_frame 不同，此版本拷贝 shared_ptr，
     * 调用者的 frame 仍然有效（引用计数 +1）。
     * 
     * 适用于需要多个消费者持有帧引用的场景：
     *   - QT 显示：ch->set_frame(frame) 持有引用
     *   - 录像编码：RecorderTask 持有引用
     *   - 两者独立释放，互不影响
     * 
     * @param f   帧数据（shared_ptr，拷贝增加引用计数）
     * @param sid 流ID
     * @param p   PTS 时间戳
     */
    static RecorderTask make_frame_shared(FramePtr& f, int sid, uint64_t p) {
        RecorderTask task;
        task.type = RecorderTaskType::FRAME;
        task.stream_id = sid;
        task.frame = f;  // shared_ptr 拷贝，引用计数 +1，f 仍然有效
        task.pts = p;
        return task;
    }

    /**
     * @brief 创建开始录像任务
     * 
     * @param sid    流ID
     * @param dev_id 设备ID（哪个 ROI 触发）
     * @param st     开始时间字符串（格式："20260528_143025"）
     */
    static RecorderTask make_start(int sid, int dev_id, const char* st) {
        RecorderTask task;
        task.type = RecorderTaskType::START;
        task.stream_id = sid;
        task.device_id = dev_id;
        strncpy(task.start_time, st, sizeof(task.start_time) - 1);
        task.start_time[sizeof(task.start_time) - 1] = '\0';
        return task;
    }

    /**
     * @brief 创建停止录像任务
     * 
     * @param sid    流ID
     * @param dev_id 设备ID（用于日志）
     * @param et     结束时间字符串
     */
    static RecorderTask make_stop(int sid, int dev_id, const char* et) {
        RecorderTask task;
        task.type = RecorderTaskType::STOP;
        task.stream_id = sid;
        task.device_id = dev_id;
        strncpy(task.end_time, et, sizeof(task.end_time) - 1);
        task.end_time[sizeof(task.end_time) - 1] = '\0';
        return task;
    }
};

/**
 * @brief 独立录像线程池
 * 
 * ================================================================
 * 架构改进（vs 旧版）：
 * ================================================================
 * 
 * 旧版问题：
 *   - 多录像线程共享一个任务队列，需要做 stream 分配
 *   - 不属于自己的 task 需要放回队列，可能死循环
 *   - 同一路 stream 可能被不同线程处理，需要额外同步
 * 
 * 新版设计：
 *   - 每路 stream 一个独立 MPMC 队列
 *   - 每路 stream 一个独立录像线程（RecorderContext）
 *   - submit 时直接放入对应 stream 的队列
 *   - 无需任务分配逻辑，一路一流一对一
 * 
 * 线程安全：
 *   - 多 Worker 并发提交到 per-stream 队列（MPMC 保证）
 *   - 每路录像器由单个线程独占，无需加锁
 *   - 原子状态避免重复 start/stop
 */
class RecorderPool {
public:
    // ============================================================
    // 【关键修复】将 RecorderContext 定义移到 public 区域最前面
    // 这样后面的 public 方法声明都能看到这个类型
    // ============================================================

    /**
     * @brief 每路录像器上下文
     * 
     * 每个 stream 一个独立的 RecorderContext，
     * 由对应的录像线程独占访问，无需加锁
     */
    struct RecorderContext {
        VideoRecorder* recorder;                    // 录像器实例
        std::atomic<bool> is_recording;             // 原子状态（Worker 可能查询）
        int current_device_id;                      // 当前触发的设备 ID
        std::chrono::steady_clock::time_point start_time;  // 录像开始时间
        char start_time_str[64];  // 新增：录像开始时间字符串
        bool is_registered;  // 新增：true=有人登记，false=无人登记/未通过

        RecorderContext()
            : recorder(nullptr)
            , is_recording(false)
            , current_device_id(-1)
            , is_registered(false)  // 新增
        {
            start_time_str[0] = '\0';  // 新增
        }

        // 禁止拷贝
        RecorderContext(const RecorderContext&) = delete;
        RecorderContext& operator=(const RecorderContext&) = delete;

        // 允许移动
        RecorderContext(RecorderContext&& other) noexcept
            : recorder(other.recorder)
            , is_recording(other.is_recording.load())
            , current_device_id(other.current_device_id)
            , start_time(other.start_time)
            , is_registered(other.is_registered)  // 新增
        {
            strncpy(start_time_str, other.start_time_str, 63);  // 新增
            other.recorder = nullptr;
            other.current_device_id = -1;
        }

        RecorderContext& operator=(RecorderContext&& other) noexcept {
            if (this != &other) {
                recorder = other.recorder;
                is_recording.store(other.is_recording.load());
                current_device_id = other.current_device_id;
                start_time = other.start_time;
                is_registered = other.is_registered;  // 新增
                strncpy(start_time_str, other.start_time_str, 63);  // 新增
                other.recorder = nullptr;
                other.current_device_id = -1;
            }
            return *this;
        }
    };

    // ============================================================
    // 公共接口
    // ============================================================

    /**
     * @brief 构造函数
     * 
     * @param channel_count 总流路数（每路一个录像线程 + 一个队列）
     */
    explicit RecorderPool(int channel_count);

    ~RecorderPool();

    /**
     * @brief 启动所有录像线程
     * 
     * 内部：
     *   1. 创建输出目录
     *   2. 为每路流创建 VideoRecorder 实例
     *   3. 启动 per-stream 录像线程
     */
    void start();

    /**
     * @brief 停止所有录像线程
     * 
     * 会等待队列中所有任务处理完毕（最多 3 秒）
     */
    void stop();

    /**
     * @brief 提交任务到指定 stream 的队列（非阻塞）
     * 
     * @param task 任务（移动语义，调用后 task 被重置）
     * @return true=成功入队，false=队列满或 stream_id 无效
     * 
     * 多 Worker 线程可并发调用，MPMC 队列保证线程安全
     */
    bool submit(RecorderTask&& task);

    /**
     * @brief 获取指定 stream 队列的待处理任务数量
     */
    size_t pending_tasks(int stream_id) const {
        if (stream_id < 0 || stream_id >= channel_count_) return 0;
        return stream_queues_[stream_id] ? stream_queues_[stream_id]->size() : 0;
    }

    /**
     * @brief 查询某路流是否正在录像
     */
    bool is_recording(int stream_id) const {
        if (stream_id < 0 || stream_id >= channel_count_) return false;
        return contexts_[stream_id].is_recording.load(std::memory_order_acquire);
    }

    void mark_registered(int stream_id, bool registered) {
        if (stream_id >= 0 && stream_id < channel_count_) {
            contexts_[stream_id].is_registered = registered;
        }
    }

private:
    // ============================================================
    // 私有方法
    // ============================================================

    /**
     * @brief Per-stream 录像线程主循环
     * 
     * @param stream_id 该线程负责的流 ID
     * 
     * 每个线程独占处理一路流的所有录像任务
     */
    void recorder_loop(int stream_id);

    /**
     * @brief 处理单个录像任务
     * 
     * @param ctx      录像器上下文
     * @param task     任务（移动后销毁）
     * @param stream_id 流 ID
     */
    void process_task(RecorderContext& ctx, RecorderTask&& task, int stream_id);

    // ============================================================
    // 私有成员变量
    // ============================================================

    int channel_count_;                                          // 总流路数
    std::vector<std::unique_ptr<MPMCQueue<RecorderTask>>> stream_queues_;  // per-stream 任务队列
    std::vector<std::thread> threads_;                           // per-stream 录像线程
    std::atomic<bool> running_;                                  // 运行标志
    std::vector<RecorderContext> contexts_;                       // per-stream 录像器上下文
};

#endif // RECORDER_QUEUE_HPP