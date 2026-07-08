#ifndef WORKER_POOL_HPP
#define WORKER_POOL_HPP

#include "mpmc_queue.hpp"
#include "yolov5_infer.h"
#include "drawing.h"
#include "scheduler.hpp"
#include "config.h"
#include "ipc/ipc_socket.h"
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

// ================================================================
// Phase2 对外接口（在 worker_pool.cpp 中实现）
// ================================================================

/**
 * @brief 热重载 ROI 配置（线程安全）
 *
 * 在收到 QT 端的保存通知后调用。会加写锁重新读取 roi.conf 到 g_roi，
 * 并把新的 device_count/device_id 同步到检测状态机（保留流级录像状态）。
 * @return 0 成功, -1 失败
 */
int phase2_reload_roi();

/**
 * @brief 轮询 ROI 重载命令（非阻塞）
 *
 * 由主循环周期性调用。若 QT 端发来重载请求则执行 phase2_reload_roi()。
 */
void phase2_poll_roi_reload();

/**
 * @brief 轮询 QT → 主进程 命令（非阻塞）
 *
 * 处理 QT 端发来的命令（如手动断电）：
 *   - MAIN_CMD_POWER_OFF: 清空设备状态 + 转发 RELEASE 给 device_process
 *
 * 由主循环周期性调用。
 */
void phase2_poll_qt_main();

/**
 * @brief 轮询 device_process 连接（非阻塞 accept）
 *
 * 主进程作为服务端监听 SOCK_PATH_MAIN_DEVICE，
 * device_process 启动后主动连接，此函数 accept 并保存 fd。
 *
 * 由主循环周期性调用。
 */
void phase2_poll_device();

/**
 * @brief 把指定路的设备占用状态填入 FrameMeta（用于 Qt ROI 框颜色/文字）
 *
 * 读取 g_state_mgr 中该路各设备的 silent_until_expire 状态：
 *   - 免打扰中 = 使用中(occupied=1)
 *   - 否则     = 空闲(occupied=0)
 *
 * @param meta      待发送的帧元数据（填充 device_count/device_ids/device_occupied）
 * @param stream_id 流 ID
 *
 * 注意：bool 读取实践上安全（单字节），显示用途可容忍偶发竞争；
 *       如需强一致可后续给状态机加自旋锁。
 */
void phase2_fill_device_status(FrameMeta* meta, int stream_id);

/**
 * @brief 轮询 face_process → main_process 的 ESP32 识别结果（非阻塞）
 *
 * face_process 识别完成后通过 SOCK_PATH_MAIN_FACE 回传 Esp32RecognizeResult。
 * main 收到后：
 *   1. 成功登记：ds_set_silent + mark_registered（标记设备使用中）
 *   2. 转发给 device_process（开插座 + MQTT 回 ESP32）
 *
 * 由主循环周期性调用。
 */
void phase2_poll_face_result();

#endif // WORKER_POOL_HPP