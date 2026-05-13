/**
 * @file worker_pool.cpp
 * @brief Worker线程池 - 重构版
 * 
 * 职责（精简后）：
 * 1. 从MPMC队列取帧
 * 2. YOLO推理 + ROI判断
 * 3. 更新状态机
 * 4. 投递录像任务到RecorderPool（使用 FramePtr 引用计数）
 * 5. 更新显示缓存
 * 6. 通过 Unix Socket 发送人脸识别触发事件到人脸进程
 * 
 * 通信方式：主进程作为Socket服务器，人脸进程作为客户端连接
 * 
 * Socket生命周期：
 *   1. phase2_init() 创建服务器 socket (ipc_sock_server_create)
 *   2. send_face_trigger_event() 中 accept() 接受客户端连接
 *   3. 后续通过已建立的连接发送/接收数据
 *   4. phase2_deinit() 关闭 socket
 */

#include "worker_pool.hpp"
#include "yolov5_infer.h"
#include "recorder_queue.hpp"
#include "roi_config.h"
#include "detection_state.h"
#include "ipc/ipc_socket.h"
#include "ipc/db.h"
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <shared_mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

// ================================================================
// 全局变量声明（在 main.cpp 中定义）
// ================================================================
extern AppConfig g_cfg;

RecorderPool*          g_recorder_pool = NULL; // 录像线程池
void*                  g_main_db = NULL;       // 数据库句柄

// ================================================================
// Phase 2 全局状态
// ================================================================
static ROIConfig              g_roi;                  // ROI配置
static DetectionStateManager  g_state_mgr;            // 状态机
static int                    g_listen_fd = -1;       // 监听 socket（服务器）
static int                    g_client_fd = -1;       // 客户端连接 socket
static bool                   g_phase2_inited = false;
static int                    g_skip_counter[MAX_CHANNEL] = {0};  // 每路跳帧计数

// ROI 热重载相关
static std::shared_mutex      g_roi_mutex;            // 读写锁：保护 g_roi
static int                    g_roi_listen_fd = -1;   // ROI 重载命令监听 socket
static int                    g_roi_client_fd = -1;   // ROI 重载命令客户端 fd

// ================================================================
// 辅助函数
// ================================================================
static long elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000 +
           (end.tv_nsec - start.tv_nsec) / 1000000;
}

// ================================================================
// WorkerPool 构造/析构
// ================================================================
WorkerPool::WorkerPool(int num_workers, MPMCQueue<FramePtr>* in_queue,
                       worker_context_t* worker_ctxs,
                       ChannelContext* channels, int channel_count)
    : num_workers_(num_workers)
    , in_queue_(in_queue)
    , worker_ctxs_(worker_ctxs)
    , channels_(channels)
    , channel_count_(channel_count)
    , running_(false)
{
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::start()
{
    if (running_) return;
    running_ = true;
    
    for (int i = 0; i < num_workers_; i++) {
        threads_.emplace_back(&WorkerPool::worker_loop, this, i);
    }
    
    printf("[WorkerPool] Started %d workers\n", num_workers_);
}

void WorkerPool::stop()
{
    if (!running_) return;
    running_ = false;
    
    for (auto& th : threads_) {
        if (th.joinable()) th.join();
    }
    threads_.clear();
    
    printf("[WorkerPool] All workers stopped\n");
}

// ================================================================
// Worker 线程主循环
// ================================================================
void WorkerPool::worker_loop(int worker_id)
{
    worker_context_t* worker_ctx = &worker_ctxs_[worker_id];
    FramePtr frame = nullptr;
    
    printf("[Worker %d] Started (NPU core %d)\n", worker_id, worker_id % 3);
    
    while (running_) {
        // 非阻塞取帧
        if (in_queue_->try_dequeue(frame)) {
            process_frame(worker_ctx, std::move(frame));
        } else {
            // 队列空，短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 退出前清空残留帧（自动析构）
    while (in_queue_->try_dequeue(frame)) {
        // frame 自动析构，释放引用
    }
    
    printf("[Worker %d] Stopped\n", worker_id);
}

// ============================================================
// 确保客户端已连接，如果没有则接受新连接
// ============================================================
static bool ensure_face_client_connected()
{
    // 如果已有客户端连接且有效，直接返回 true
    if (g_client_fd >= 0) {
        return true;
    }
    
    // 检查监听 socket 是否有效
    if (g_listen_fd < 0) {
        return false;
    }
    
    // 尝试接受新连接（非阻塞）
    int client_fd = accept(g_listen_fd, NULL, NULL);
    if (client_fd >= 0) {
        // 设置非阻塞模式
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        }
        g_client_fd = client_fd;
        printf("[Worker] Face process connected (fd=%d)\n", client_fd);
        return true;
    }
    
    return false;
}

// ================================================================
// 发送人脸识别触发事件到人脸进程（通过 Socket）
// ================================================================
static void send_face_trigger_event(int stream_id, int device_id, int duration_minutes)
{
    // ★ 先确保客户端已连接
    if (!ensure_face_client_connected()) {
        printf("[Worker] Warning: face client not connected, cannot send event\n");
        return;
    }
    
    // 构建事件结构体（与 face_service 中定义的一致）
    FaceEvent event;
    memset(&event, 0, sizeof(event));
    event.room_id = stream_id;
    event.device_id = device_id;
    event.duration_minutes = duration_minutes;
    event.confirm_duration = 0;
    
    // 发送到人脸进程
    ssize_t n = send(g_client_fd, &event, sizeof(event), 0);
    if (n != sizeof(event)) {
        printf("[Worker] Failed to send face trigger event: %zd\n", n);
        // 发送失败，关闭连接，下次会重新 accept
        close(g_client_fd);
        g_client_fd = -1;
    } else {
        printf("[Worker] Sent face trigger: stream=%d, device=%d\n", stream_id, device_id);
    }
}

// ================================================================
// 接收人脸识别确认结果（从 Socket 读取）
// ============================================================
static bool receive_face_confirm(int& room_id, int& device_id, int& duration)
{
    if (g_client_fd < 0) return false;
    
    // 设置超时：非阻塞读取
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms 超时
    setsockopt(g_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    FaceEvent event;
    ssize_t n = recv(g_client_fd, &event, sizeof(event), 0);
    
    if (n == sizeof(event)) {
        room_id = event.room_id;
        device_id = event.device_id;
        duration = event.confirm_duration;
        return true;
    }
    
    // 如果连接断开，关闭 fd
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(g_client_fd);
        g_client_fd = -1;
    }
    
    return false;
}

// ================================================================
// 处理单帧（核心逻辑）
// ================================================================
void WorkerPool::process_frame(worker_context_t* worker_ctx, FramePtr frame)
{
    // 空指针检查
    if (!frame) {
        printf("[Worker %d] Warning: null frame\n", worker_ctx->worker_id);
        return;
    }
    // 检查 DMA-BUF fd 有效性
    if (frame->is_dmabuf && frame->img.fd < 0) {
        printf("[Worker %d] Warning: invalid dmabuf fd %d\n", 
               worker_ctx->worker_id, frame->img.fd);
        return;
    }
    // 检查虚拟地址
    if (!frame->is_dmabuf && !frame->img.virt_addr) {
        printf("[Worker %d] Warning: no virt_addr\n", worker_ctx->worker_id);
        return;
    }
    
    int stream_id = frame->stream_id;
    if (stream_id < 0 || stream_id >= channel_count_) return;

    ChannelContext* ch = &channels_[stream_id];

    // ===== 1. 全部帧：推显示 + 喂录像 =====
    ch->set_frame(frame);

    // 喂帧给录像线程
    if (g_recorder_pool && frame) {
        RecorderTask task = RecorderTask::make_frame_shared(frame, stream_id, frame->pts);
        g_recorder_pool->submit(std::move(task));
    }
    
    // ===== 跳帧推理：每路每2帧推理1帧 =====
    g_skip_counter[stream_id]++;
    if (g_skip_counter[stream_id] % 2 != 0) return;

    // ============================================================
    // 1. YOLO推理
    // ============================================================
    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));
    
    if (yolov5_infer(worker_ctx, &frame->img, &od_results) != 0) {
        return;
    }
    
    // ============================================================
    // 2. 保存检测框到 ChannelContext
    // ============================================================
    DetectionBox boxes[MAX_DETECTIONS];
    int cnt = 0;
    
    for (int i = 0; i < od_results.count && cnt < MAX_DETECTIONS; i++) {
        if (od_results.results[i].cls_id == 0) {
            boxes[cnt].x = od_results.results[i].box.left;
            boxes[cnt].y = od_results.results[i].box.top;
            boxes[cnt].w = od_results.results[i].box.right - od_results.results[i].box.left;
            boxes[cnt].h = od_results.results[i].box.bottom - od_results.results[i].box.top;
            cnt++;
        }
    }
    ch->set_boxes(boxes, cnt);
    
    // ============================================================
    // 3. Phase2: ROI检测 + 状态机 + 录像任务投递
    // ============================================================
    if (!g_phase2_inited) return;

    // 3.0 在读锁保护下拷贝本路 ROI 配置（避免与热重载竞争）
    StreamROIConfig sc_local;
    {
        std::shared_lock<std::shared_mutex> lk(g_roi_mutex);
        if (stream_id >= g_roi.stream_count) return;
        sc_local = g_roi.streams[stream_id];   // 结构体小，直接拷贝
    }

    // 3.1 收集检测框坐标数组
    int all_boxes[MAX_DETECTIONS][4];
    int box_count = 0;
    for (int i = 0; i < od_results.count && box_count < MAX_DETECTIONS; i++) {
        if (od_results.results[i].cls_id == 0) {
            all_boxes[box_count][0] = od_results.results[i].box.left;
            all_boxes[box_count][1] = od_results.results[i].box.top;
            all_boxes[box_count][2] = od_results.results[i].box.right - od_results.results[i].box.left;
            all_boxes[box_count][3] = od_results.results[i].box.bottom - od_results.results[i].box.top;
            box_count++;
        }
    }

    // 3.2 遍历每个设备ROI
    for (int d = 0; d < sc_local.device_count; d++) {
        DeviceROI* roi = &sc_local.devices[d];

        int event = ds_update(&g_state_mgr, all_boxes, box_count, stream_id, roi);
        
        // ★ 事件bit0: 触发人脸识别（通过 Socket 发送）
        if (event & 1) {
            printf("[Worker %d] Trigger face recognition: stream=%d, device=%d\n",
                   worker_ctx->worker_id, stream_id, roi->device_id);
            
            // ★ 改为通过 Socket 发送事件，不再使用共享内存
            send_face_trigger_event(stream_id, roi->device_id, 15);
        }
        
        // 事件bit1: 开始录像
        if (event & 2) {
            if (g_recorder_pool) {
                time_t t = time(NULL);
                char start_time[64];
                strftime(start_time, sizeof(start_time), "%Y%m%d_%H%M%S", localtime(&t));
                
                RecorderTask task = RecorderTask::make_start(stream_id, roi->device_id, start_time);
                g_recorder_pool->submit(std::move(task));
            }
        }
        
        // 事件bit2: 停止录像
        if (event & 4) {
            if (g_recorder_pool) {
                time_t t = time(NULL);
                char end_time[64];
                strftime(end_time, sizeof(end_time), "%Y%m%d_%H%M%S", localtime(&t));
                
                RecorderTask task = RecorderTask::make_stop(stream_id, roi->device_id, end_time);
                g_recorder_pool->submit(std::move(task));
            }
        }
    }
    
    // ============================================================
    // 4. 检查人脸识别确认结果（从 Socket 读取）
    // ============================================================
    int room, dev, dur;
    if (receive_face_confirm(room, dev, dur)) {
        if (dur > 0) {
            printf("[Worker %d] Face confirmed: room=%d, device=%d, silent=%d min\n",
                   worker_ctx->worker_id, room, dev, dur);
            ds_set_silent(&g_state_mgr, room, dev, dur);

            if (g_recorder_pool) {
                g_recorder_pool->mark_registered(room, true);
            }
        } else {
            printf("[Worker %d] Face recognition failed\n", worker_ctx->worker_id);
        }
    }
}

// ================================================================
// Phase2 初始化
// ================================================================
void phase2_init(const char* db_path)
{
    // 1. 加载ROI配置（加写锁，保持与热重载一致的访问规约）
    {
        std::unique_lock<std::shared_mutex> lk(g_roi_mutex);
        roi_config_load("../../config/roi.conf", &g_roi);
    }
    
    // 2. 初始化状态机
    ds_init(&g_state_mgr, g_roi.person_stay_threshold, g_roi.absence_threshold);
    
    // 3. 同步device_count到状态机
    for (int i = 0; i < g_roi.stream_count && i < g_cfg.channel_count; i++) {
        g_state_mgr.streams[i].device_count = g_roi.streams[i].device_count;
        g_state_mgr.streams[i].stream_id = i;
        printf("[Phase2] Stream %d has %d devices\n", i, g_roi.streams[i].device_count);
    }
    
    // 4. 创建录像线程池
    g_recorder_pool = new RecorderPool(g_cfg.channel_count);
    g_recorder_pool->start();
    
    // ================================================================
    // 5. ★ 创建 Socket 服务器（等待人脸进程连接）
    // ================================================================
    g_listen_fd = ipc_sock_server_create(SOCK_PATH_MAIN_FACE, 5);
    if (g_listen_fd < 0) {
        printf("[Phase2] Warning: Failed to create face socket: %s\n", SOCK_PATH_MAIN_FACE);
    } else {
        printf("[Phase2] Face socket created: %s (listening)\n", SOCK_PATH_MAIN_FACE);
        // 设置为非阻塞模式，避免 accept 阻塞
        int flags = fcntl(g_listen_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(g_listen_fd, F_SETFL, flags | O_NONBLOCK);
        }
        g_client_fd = -1;  // 初始无客户端连接
    }

    // ================================================================
    // 5.1 ★ 创建 ROI 重载命令 Socket 服务器（等待 QT 连接）
    // ================================================================
    g_roi_listen_fd = ipc_sock_server_create(SOCK_PATH_MAIN_ROI, 5);
    if (g_roi_listen_fd < 0) {
        printf("[Phase2] Warning: Failed to create roi socket: %s\n", SOCK_PATH_MAIN_ROI);
    } else {
        int rflg = fcntl(g_roi_listen_fd, F_GETFL, 0);
        if (rflg >= 0) {
            fcntl(g_roi_listen_fd, F_SETFL, rflg | O_NONBLOCK);
        }
        g_roi_client_fd = -1;
        printf("[Phase2] ROI reload socket created: %s (listening)\n", SOCK_PATH_MAIN_ROI);
    }
    
    // 6. 打开数据库
    g_main_db = db_open(db_path);
    
    g_phase2_inited = true;
    printf("[Phase2] Initialized: %d streams, stay=%dms, absence=%dms\n",
           g_roi.stream_count, g_roi.person_stay_threshold, g_roi.absence_threshold);
}

// ================================================================
// Phase2 清理
// ================================================================
void phase2_deinit()
{
    // 1. 停止录像线程池
    if (g_recorder_pool) {
        delete g_recorder_pool;
        g_recorder_pool = nullptr;
    }
    
    // 2. 关闭数据库
    if (g_main_db) {
        db_close(g_main_db);
        g_main_db = nullptr;
    }
    
    // 3. 关闭客户端连接
    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }

    // 4. 关闭监听 Socket
    if (g_listen_fd >= 0) {
        ipc_sock_close(g_listen_fd, SOCK_PATH_MAIN_FACE);
        g_listen_fd = -1;
    }

    // 4.1 关闭 ROI 重载 Socket
    if (g_roi_client_fd >= 0) {
        close(g_roi_client_fd);
        g_roi_client_fd = -1;
    }
    if (g_roi_listen_fd >= 0) {
        ipc_sock_close(g_roi_listen_fd, SOCK_PATH_MAIN_ROI);
        g_roi_listen_fd = -1;
    }

    g_phase2_inited = false;
    printf("[Phase2] Cleaned up\n");
}

// ================================================================
// ROI 热重载（线程安全）
// ================================================================
int phase2_reload_roi()
{
    if (!g_phase2_inited) return -1;

    // 写锁：阻塞 worker 的读访问，整段重载+同步状态机
    std::unique_lock<std::shared_mutex> lk(g_roi_mutex);

    ROIConfig new_cfg;
    if (roi_config_load("../../config/roi.conf", &new_cfg) != 0) {
        printf("[Phase2] ROI reload failed: load error\n");
        return -1;
    }

    g_roi = new_cfg;
    // 同步到状态机（保留流级录像状态，仅重置设备级检测状态）
    ds_apply_roi(&g_state_mgr, &g_roi);

    printf("[Phase2] ROI reloaded: %d streams\n", g_roi.stream_count);
    for (int i = 0; i < g_roi.stream_count; i++) {
        printf("  流%d: %d 设备\n", i, g_roi.streams[i].device_count);
    }
    return 0;
}

// ================================================================
// 轮询 ROI 重载命令（非阻塞，主循环调用）
// ================================================================
void phase2_poll_roi_reload()
{
    if (!g_phase2_inited) return;
    if (g_roi_listen_fd < 0) return;

    // 1. 没有客户端则非阻塞 accept
    if (g_roi_client_fd < 0) {
        int fd = accept(g_roi_listen_fd, NULL, NULL);
        if (fd >= 0) {
            int flg = fcntl(fd, F_GETFL, 0);
            if (flg >= 0) fcntl(fd, F_SETFL, flg | O_NONBLOCK);
            g_roi_client_fd = fd;
            printf("[Phase2] ROI reload client connected fd=%d\n", fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("[Phase2] roi accept");
        }
    }

    if (g_roi_client_fd < 0) return;

    // 2. 非阻塞读取命令字节：'R'=重载
    char cmd = 0;
    ssize_t n = recv(g_roi_client_fd, &cmd, 1, 0);
    if (n > 0) {
        if (cmd == 'R') {
            printf("[Phase2] ROI reload request received\n");
            phase2_reload_roi();
            // 回复一个字节确认
            char ack = 'O';
            send(g_roi_client_fd, &ack, 1, 0);
        }
    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        // 客户端断开
        close(g_roi_client_fd);
        g_roi_client_fd = -1;
    }
}

// ================================================================
// 把指定路的设备占用状态填入 FrameMeta（供 Qt 显示空闲/使用中）
// 在读锁保护下读 g_roi 的 device_id，读 g_state_mgr 的 silent 状态。
// g_state_mgr 本身无锁，bool 读取实践安全，显示用途可容忍偶发竞争。
// ================================================================
void phase2_fill_device_status(FrameMeta* meta, int stream_id)
{
    if (!meta || stream_id < 0 || stream_id >= MAX_CHANNEL) {
        if (meta) meta->device_count = 0;
        return;
    }
    if (!g_phase2_inited) {
        meta->device_count = 0;
        return;
    }

    // 读 ROI 配置拿 device_id 列表（读锁）
    StreamROIConfig sc;
    {
        std::shared_lock<std::shared_mutex> lk(g_roi_mutex);
        if (stream_id >= g_roi.stream_count) {
            meta->device_count = 0;
            return;
        }
        sc = g_roi.streams[stream_id];
    }

    int n = sc.device_count;
    if (n < 0) n = 0;
    if (n > MAX_DEVICES_EACH) n = MAX_DEVICES_EACH;

    meta->device_count = (uint32_t)n;
    for (int d = 0; d < n; d++) {
        meta->device_ids[d] = sc.devices[d].device_id;
        // 在状态机里找对应设备，取 silent_until_expire
        bool occupied = false;
        StreamState* ss = &g_state_mgr.streams[stream_id];
        for (int k = 0; k < ss->device_count && k < MAX_DEVICES_EACH; k++) {
            if (ss->devices[k].device_id == sc.devices[d].device_id) {
                occupied = ss->devices[k].silent_until_expire;
                break;
            }
        }
        meta->device_occupied[d] = occupied ? 1u : 0u;
    }
}