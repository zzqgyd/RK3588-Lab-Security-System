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
#include <mutex>
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

// 设备进程 IPC 相关（主进程作为服务端，device_process 主动连接）
static int                    g_device_listen_fd = -1;  // 监听 fd（SOCK_PATH_MAIN_DEVICE）
static int                    g_device_client_fd = -1;  // device_process 连接的 fd
// 串行化 send_device_event 的 send+recv（多 worker 并发会串 ack）
static std::mutex             g_device_mutex;

// QT → 主进程 命令 socket（手动断电等）
static int                    g_qtmain_listen_fd = -1;  // 监听 fd
static int                    g_qtmain_client_fd = -1;  // 客户端 fd

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

// ================================================================
// 设备进程 IPC 服务端
// ----------------------------------------------------------------
// 主进程作为服务端监听 SOCK_PATH_MAIN_DEVICE
// device_process 启动后主动连接，保持长连接
// 设备登记成功时发 REGISTER，手动断电/超时清状态时发 RELEASE
// ================================================================

// 非阻塞 accept device_process 连接
// 在主循环中调用，accept 成功后保存 fd
static void accept_device_connection()
{
    if (g_device_listen_fd < 0) return;

    int fd = accept(g_device_listen_fd, NULL, NULL);
    if (fd < 0) {
        // EAGAIN/EWOULDBLOCK 表示无新连接，正常
        return;
    }

    // ★ 设置 O_NONBLOCK，确保所有 recv/send 都不阻塞
    int flg = fcntl(fd, F_GETFL, 0);
    if (flg >= 0) fcntl(fd, F_SETFL, flg | O_NONBLOCK);

    // 如果有旧连接，先关闭（try_lock 避免阻塞主循环）
    {
        std::unique_lock<std::mutex> lk(g_device_mutex, std::try_to_lock);
        if (!lk.owns_lock()) {
            // worker 正在 send，延迟到下一轮再 accept
            close(fd);
            return;
        }
        if (g_device_client_fd >= 0) {
            close(g_device_client_fd);
        }
        g_device_client_fd = fd;
    }
    printf("[Worker] device_process connected (fd=%d)\n", fd);
}

// ================================================================
// ESP32 识别流程：转发函数
// ----------------------------------------------------------------
// main 在 ESP32 流程中充当中转：
//   device → main(tag=1) → face → main → device
//   device → main(tag=2) → main 自行处理(RELEASE/CANCEL)
// ================================================================

// 确保 face_process 已连接（非阻塞 accept）
static bool ensure_face_client_connected()
{
    if (g_client_fd >= 0) return true;
    if (g_listen_fd < 0) return false;

    int fd = accept(g_listen_fd, NULL, NULL);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        g_client_fd = fd;
        printf("[Worker] Face process connected (fd=%d)\n", fd);
        return true;
    }
    return false;
}

// 转发 Esp32RecognizeEvent 给 face_process（main → face）
static void forward_esp32_event_to_face(const Esp32RecognizeEvent& ev)
{
    if (!ensure_face_client_connected()) {
        printf("[Worker] face_process 未连接，丢弃 ESP32 识别请求 task=%d\n", ev.task_id);
        return;
    }
    ssize_t n = send(g_client_fd, &ev, sizeof(ev), 0);
    if (n != sizeof(ev)) {
        printf("[Worker] send Esp32RecognizeEvent failed: %zd\n", n);
        close(g_client_fd);
        g_client_fd = -1;
    } else {
        printf("[Worker] → face: ESP32 识别请求 task=%d 房间%d 设备%d %s\n",
               ev.task_id, ev.room_id, ev.device_id, ev.rtsp_url);
    }
}

// 处理 ESP32 发起的 DeviceEvent（RELEASE=终止 / event=4=超时取消）
static void handle_esp32_device_event(const DeviceEvent& ev)
{
    printf("[Worker] ESP32 事件: event=%d 房间%d 设备%d\n",
           ev.event, ev.room_id, ev.device_id);
    // 两种情况都清检测状态 + 取消录像标记
    ds_reset_device(&g_state_mgr, ev.room_id, ev.device_id);
    if (g_recorder_pool) {
        g_recorder_pool->mark_registered(ev.room_id, false);
    }
}

// 转发 Esp32RecognizeResult 给 device_process（main → device）
// ★ fire-and-forget：只发送，不等 ack。ack 由主循环 phase2_poll_device 消费
// ★ try_lock：锁忙时丢弃（识别结果丢失概率极低，本地 IPC 亚毫秒）
// ★ tag=1 + Esp32RecognizeResult(88 bytes)
static bool forward_esp32_result_to_device(const Esp32RecognizeResult& result)
{
    std::unique_lock<std::mutex> lk(g_device_mutex, std::try_to_lock);
    if (!lk.owns_lock()) {
        printf("[Worker] device_process 锁忙，丢弃识别结果 task=%d\n", result.task_id);
        return false;
    }

    if (g_device_client_fd < 0) {
        printf("[Worker] device_process 未连接，丢弃识别结果 task=%d\n", result.task_id);
        return false;
    }

    // ★ 合并 tag+payload 到单次 send，保证原子性，避免 device 端读到半条
    struct { int tag; Esp32RecognizeResult result; } pkt;
    pkt.tag    = 1;
    pkt.result = result;
    ssize_t n = send(g_device_client_fd, &pkt, sizeof(pkt), MSG_DONTWAIT);
    if (n != sizeof(pkt)) {
        printf("[Worker] send Esp32RecognizeResult failed，丢弃 task=%d\n", result.task_id);
        return false;
    }

    // ★ 不等 ack！直接返回。ack(tag=3) 由 phase2_poll_device 消费
    return true;
}

// 发送事件给 device_process（fire-and-forget，完全不阻塞）
//   event: DEVICE_EVENT_REGISTER / DEVICE_EVENT_RELEASE
//   ★ 只发送，不等 ack。ack 由主循环 phase2_poll_device 异步消费
//   ★ try_lock：锁忙时丢弃事件（REGISTER/RELEASE 都是幂等的，丢一次没关系）
//   ★ MSG_DONTWAIT：send 非阻塞，socket buffer 满时丢弃
//
// ★★ Tag-based protocol (main → device):
//   先发 4 字节 tag，再发 payload，避免 device 端 MSG_PEEK 按长度匹配
//   （多消息合并时 MSG_PEEK 长度不等于任何单条消息大小，导致死循环）
//   tag=0: DeviceEvent(16 bytes)
//   tag=1: Esp32RecognizeResult(88 bytes)
static bool send_device_event(int event, int room_id, int device_id, int duration_minutes)
{
    // ★ try_lock：锁被占用时丢弃事件，绝不阻塞 worker 线程
    std::unique_lock<std::mutex> lk(g_device_mutex, std::try_to_lock);
    if (!lk.owns_lock()) {
        return false;  // 另一个 worker 在用，跳过
    }

    if (g_device_client_fd < 0) {
        return false;
    }

    // ★ 合并 tag+payload 到单次 send，保证原子性，避免 device 端读到半条
    DeviceEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event           = event;
    ev.room_id         = room_id;
    ev.device_id       = device_id;
    ev.duration_minutes = duration_minutes;

    struct { int tag; DeviceEvent ev; } pkt;
    pkt.tag = 0;
    pkt.ev  = ev;
    ssize_t n = send(g_device_client_fd, &pkt, sizeof(pkt), MSG_DONTWAIT);
    if (n != sizeof(pkt)) {
        // buffer 满或连接断开，丢弃事件（幂等，下次会重试）
        return false;
    }

    // ★ 不等 ack！直接返回。ack 由 phase2_poll_device 在主循环中消费
    return true;
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

        // ★ 事件bit0: 设备区域有人超阈值 → 通知 device_process 触发 ESP32 识别流程
        //   （USB 被动识别已废弃，改由 ESP32 推流 → face_process 拉流识别）
        //   device_process 收到 REGISTER 后：MQTT 通知 ESP32 → 等 ready → 拉流识别
        //   识别结果异步回传：device → main → face → main → device
        if (event & 1) {
            printf("[Worker %d] 触发设备登记: stream=%d, device=%d -> 通知 device_process (ESP32流程)\n",
                   worker_ctx->worker_id, stream_id, roi->device_id);
            send_device_event(DEVICE_EVENT_REGISTER, stream_id, roi->device_id, 15);
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
    // 4. ESP32 识别结果处理已移至 phase2_poll_face_result()
    //    （异步流程：device → main → face → main → device）
    // ============================================================
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

    // ================================================================
    // 5.2 ★ 创建 QT → 主进程 命令 Socket（手动断电等）
    // ================================================================
    g_qtmain_listen_fd = ipc_sock_server_create(SOCK_PATH_QT_MAIN, 5);
    if (g_qtmain_listen_fd < 0) {
        printf("[Phase2] Warning: Failed to create qt-main socket: %s\n", SOCK_PATH_QT_MAIN);
    } else {
        int qflg = fcntl(g_qtmain_listen_fd, F_GETFL, 0);
        if (qflg >= 0) {
            fcntl(g_qtmain_listen_fd, F_SETFL, qflg | O_NONBLOCK);
        }
        g_qtmain_client_fd = -1;
        printf("[Phase2] QT-main cmd socket created: %s (listening)\n", SOCK_PATH_QT_MAIN);
    }

    // ================================================================
    // 5.3 ★ 创建 主进程 → 设备进程 事件 Socket（主进程作为服务端）
    // ----------------------------------------------------------------
    // device_process 启动后主动连接此 socket，保持长连接
    // 主进程通过此连接发送 REGISTER / RELEASE 事件
    // ================================================================
    g_device_listen_fd = ipc_sock_server_create(SOCK_PATH_MAIN_DEVICE, 5);
    if (g_device_listen_fd < 0) {
        printf("[Phase2] Warning: Failed to create device socket: %s\n", SOCK_PATH_MAIN_DEVICE);
    } else {
        int dflg = fcntl(g_device_listen_fd, F_GETFL, 0);
        if (dflg >= 0) {
            fcntl(g_device_listen_fd, F_SETFL, dflg | O_NONBLOCK);
        }
        g_device_client_fd = -1;
        printf("[Phase2] Device socket created: %s (listening, waiting for device_process)\n",
               SOCK_PATH_MAIN_DEVICE);
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

    // 4.2 关闭 QT → 主进程 命令 Socket
    if (g_qtmain_client_fd >= 0) {
        close(g_qtmain_client_fd);
        g_qtmain_client_fd = -1;
    }
    if (g_qtmain_listen_fd >= 0) {
        ipc_sock_close(g_qtmain_listen_fd, SOCK_PATH_QT_MAIN);
        g_qtmain_listen_fd = -1;
    }

    // 4.3 关闭 device_process 服务端 Socket
    {
        std::lock_guard<std::mutex> lk(g_device_mutex);
        if (g_device_client_fd >= 0) {
            close(g_device_client_fd);
            g_device_client_fd = -1;
        }
    }
    if (g_device_listen_fd >= 0) {
        ipc_sock_close(g_device_listen_fd, SOCK_PATH_MAIN_DEVICE);
        g_device_listen_fd = -1;
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
// phase2_poll_qt_main：轮询 QT → 主进程 命令（非阻塞）
// ----------------------------------------------------------------
// 目前支持：
//   MAIN_CMD_POWER_OFF: 手动断电
//     1. 清空检测状态机的设备状态（结束免打扰、允许重新触发）
//     2. 通知录像线程池该房间取消"已登记"标记
//     3. 转发 RELEASE 事件给 device_process（关闭插座 + 取消倒计时）
//     4. 回 MainAck 给 QT
// ================================================================
void phase2_poll_qt_main()
{
    if (!g_phase2_inited) return;
    if (g_qtmain_listen_fd < 0) return;

    // 1. 没有客户端则非阻塞 accept
    if (g_qtmain_client_fd < 0) {
        int fd = accept(g_qtmain_listen_fd, NULL, NULL);
        if (fd >= 0) {
            int flg = fcntl(fd, F_GETFL, 0);
            if (flg >= 0) fcntl(fd, F_SETFL, flg | O_NONBLOCK);
            g_qtmain_client_fd = fd;
            printf("[Phase2] QT-main client connected fd=%d\n", fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("[Phase2] qt-main accept");
        }
    }

    if (g_qtmain_client_fd < 0) return;

    // 2. 非阻塞读取 MainCmd
    MainCmd mcmd;
    memset(&mcmd, 0, sizeof(mcmd));
    ssize_t n = recv(g_qtmain_client_fd, &mcmd, sizeof(mcmd), 0);
    if (n <= 0) {
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(g_qtmain_client_fd);
            g_qtmain_client_fd = -1;
        }
        return;
    }
    if (n != sizeof(mcmd)) {
        printf("[Phase2] qt-main bad cmd len: %zd\n", n);
        return;
    }

    // 3. 处理命令
    MainAck ack;
    memset(&ack, 0, sizeof(ack));
    ack.result = 0;

    switch (mcmd.cmd) {
        case MAIN_CMD_POWER_OFF: {
            int room = mcmd.room_id;
            int dev  = mcmd.device_id;
            printf("[Phase2] QT manual power-off: room=%d device=%d\n", room, dev);

            // (1) 清空检测状态机的设备状态（结束使用，允许重新登记）
            ds_reset_device(&g_state_mgr, room, dev);

            // (2) 取消录像"已登记"标记
            if (g_recorder_pool) {
                g_recorder_pool->mark_registered(room, false);
            }

            // (3) 转发 RELEASE 给 device_process（关闭插座 + 取消倒计时）
            send_device_event(DEVICE_EVENT_RELEASE, room, dev, 0);
            break;
        }
        default:
            printf("[Phase2] unknown qt-main cmd: %d\n", mcmd.cmd);
            ack.result = -1;
    }

    // 4. 回执
    send(g_qtmain_client_fd, &ack, sizeof(ack), 0);
}

// ================================================================
// phase2_poll_device：轮询 device_process 连接 + 消息（非阻塞）
// ----------------------------------------------------------------
// 在主循环中调用：
//   1. accept device_process 的连接请求
//   2. 非阻塞 recv device→main 的所有待处理消息（循环排空）
//      tag=0: DeviceEvent ack（fire-and-forget 的 ack，读掉即可）
//      tag=1: Esp32RecognizeEvent → 转发给 face_process
//      tag=2: DeviceEvent(ESP32 RELEASE/CANCEL) → 清检测状态
//      tag=3: Esp32RecognizeResult ack（读掉即可）
//
// ★★ Peek-then-consume 模式：
//   先 MSG_PEEK 整条消息（tag+payload），只有全部到齐才消费。
//   避免消费 tag 后 payload 尚未到达 → 下次把 payload 字节误读为 tag
//   导致协议失步。
// ================================================================
void phase2_poll_device()
{
    accept_device_connection();

    if (g_device_client_fd < 0) return;

    // try_lock：worker 正在 send 时跳过
    if (!g_device_mutex.try_lock()) return;

    // ★ 循环排空所有待处理消息（最多 32 条，防止极端情况卡住主循环）
    for (int i = 0; i < 32; i++) {
        // ★ 1. peek tag（4字节，不消费）
        int tag = -1;
        ssize_t n = recv(g_device_client_fd, &tag, sizeof(tag), MSG_PEEK | MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0) {
                // 连接断开
                close(g_device_client_fd);
                g_device_client_fd = -1;
            }
            // EAGAIN = 无数据，正常
            break;
        }
        if (n < (ssize_t)sizeof(tag)) {
            break;  // tag 不完整，下次再读
        }

        // ★ 2. 根据 tag 决定 payload 大小
        size_t payload_size = 0;
        if (tag == 0 || tag == 2) {
            payload_size = sizeof(DeviceEvent);          // 16
        } else if (tag == 1) {
            payload_size = sizeof(Esp32RecognizeEvent);  // 148
        } else if (tag == 3) {
            payload_size = sizeof(int);                  // 4
        } else {
            // 未知 tag：消费掉 tag 防止死循环
            fprintf(stderr, "[Worker] poll_device: 未知 tag=%d，丢弃\n", tag);
            recv(g_device_client_fd, &tag, sizeof(tag), MSG_DONTWAIT);
            continue;
        }

        // ★ 3. peek 整条消息（tag+payload），确认全部到齐才消费
        size_t total = sizeof(tag) + payload_size;
        char peek_buf[256];  // 足够容纳最大消息（4+148=152）
        n = recv(g_device_client_fd, peek_buf, total, MSG_PEEK | MSG_DONTWAIT);
        if (n < (ssize_t)total) {
            break;  // 整条未到齐，下次再读（不消费任何字节）
        }

        // ★ 4. 整条到齐，消费 tag（此时 payload 一定可读全）
        n = recv(g_device_client_fd, &tag, sizeof(tag), MSG_DONTWAIT);
        if (n != (ssize_t)sizeof(tag)) break;

        // ★ 5. 消费 payload
        if (tag == 1) {
            // Esp32RecognizeEvent → 转发给 face_process
            Esp32RecognizeEvent esp_ev;
            memset(&esp_ev, 0, sizeof(esp_ev));
            n = recv(g_device_client_fd, &esp_ev, sizeof(esp_ev), MSG_DONTWAIT);
            if (n != (ssize_t)sizeof(esp_ev)) break;
            // 解锁后再转发（forward 内部会加锁 face_client）
            g_device_mutex.unlock();
            forward_esp32_event_to_face(esp_ev);
            // 重新加锁继续排空
            if (!g_device_mutex.try_lock()) return;
        } else if (tag == 2) {
            // DeviceEvent（ESP32 RELEASE/CANCEL）
            DeviceEvent dev_ev;
            memset(&dev_ev, 0, sizeof(dev_ev));
            n = recv(g_device_client_fd, &dev_ev, sizeof(dev_ev), MSG_DONTWAIT);
            if (n != (ssize_t)sizeof(dev_ev)) break;
            g_device_mutex.unlock();
            handle_esp32_device_event(dev_ev);
            if (!g_device_mutex.try_lock()) return;
        } else if (tag == 0) {
            // DeviceEvent ack（fire-and-forget），读掉 payload 即可
            DeviceEvent ack;
            recv(g_device_client_fd, &ack, sizeof(ack), MSG_DONTWAIT);
        } else if (tag == 3) {
            // Esp32RecognizeResult ack，读掉 payload 即可
            int ack_val = 0;
            recv(g_device_client_fd, &ack_val, sizeof(ack_val), MSG_DONTWAIT);
        }
    }

    g_device_mutex.unlock();
}

// ================================================================
// phase2_poll_face_result：轮询 face→main 识别结果（非阻塞）
// ----------------------------------------------------------------
// face_process 识别完成后发送 Esp32RecognizeResult
// main 收到后：
//   1. 成功：ds_set_silent + mark_registered（标记设备使用中）
//   2. 转发给 device_process（开插座 + MQTT 回 ESP32）
// ================================================================
void phase2_poll_face_result()
{
    if (!g_phase2_inited) return;
    if (g_client_fd < 0) {
        ensure_face_client_connected();
        return;
    }

    // 非阻塞 peek
    char peek_buf[sizeof(Esp32RecognizeResult)];
    ssize_t pn = recv(g_client_fd, peek_buf, sizeof(peek_buf), MSG_PEEK);
    if (pn < (ssize_t)sizeof(Esp32RecognizeResult)) {
        // 无数据或连接断开
        if (pn == 0 || (pn < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(g_client_fd);
            g_client_fd = -1;
        }
        return;
    }

    // 读完整的 Esp32RecognizeResult
    Esp32RecognizeResult result;
    memset(&result, 0, sizeof(result));
    ssize_t n = recv(g_client_fd, &result, sizeof(result), 0);
    if (n != sizeof(result)) {
        fprintf(stderr, "[Worker] recv Esp32RecognizeResult failed: %zd\n", n);
        close(g_client_fd);
        g_client_fd = -1;
        return;
    }

    printf("[Worker] ← face: 识别结果 task=%d success=%d user=%s 房间%d 设备%d\n",
           result.task_id, result.success, result.person_name,
           result.room_id, result.device_id);

    // 1. 成功：标记设备使用中（设置 silent + 录像标记）
    if (result.success && result.duration_minutes > 0) {
        ds_set_silent(&g_state_mgr, result.room_id, result.device_id,
                      result.duration_minutes);
        if (g_recorder_pool) {
            g_recorder_pool->mark_registered(result.room_id, true);
        }
        printf("[Worker] 设备登记成功: 房间%d 设备%d (%d分钟) 用户=%s\n",
               result.room_id, result.device_id, result.duration_minutes,
               result.person_name);
    } else if (result.success) {
        // 签到/签退成功（duration=0），不需要设 silent
        printf("[Worker] 考勤记录: %s 房间%d\n",
               result.person_name, result.room_id);
    } else {
        printf("[Worker] 识别失败 task=%d\n", result.task_id);
    }

    // 2. 转发给 device_process（开插座 / MQTT 回 ESP32）
    //    USB 主动登记(task_id=-1)只做状态标记，不转发给 device
    //    （开插座由 QT 端 requestRegister 单独完成，避免重复）
    if (result.task_id >= 0) {
        forward_esp32_result_to_device(result);
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