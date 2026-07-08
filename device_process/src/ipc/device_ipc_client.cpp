// ================================================================
// DeviceIpcClient 实现
// ----------------------------------------------------------------
// 设备进程作为纯客户端，主动连接 QT 和 main 的服务端：
//   - SOCK_PATH_QT_DEVICE   (QT 创建服务端)  → device connect
//   - SOCK_PATH_MAIN_DEVICE (main 创建服务端) → device connect
//
// 断线自动重连，连接保持长连接
// ================================================================

#include "device_ipc_client.h"
#include "ipc/ipc_socket.h"
#include "ipc/db.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <chrono>

// ================================================================
// 构造 / 析构
// ================================================================
DeviceIpcClient::DeviceIpcClient(DeviceManager& mgr, Esp32Manager& esp32)
    : mgr_(mgr), esp32_(esp32)
{
    // 注册 Esp32Manager 的事件回调
    esp32_.setEventCallback(
        [this](const Esp32EventMsg& msg) { this->onEsp32Event(msg); });
}

DeviceIpcClient::~DeviceIpcClient()
{
    stop();
}

// ================================================================
// start：启动两个连接线程
// ================================================================
bool DeviceIpcClient::start()
{
    if (running_.exchange(true)) {
        return true;  // 已启动
    }

    qt_thread_   = std::thread([this] { qtConnectLoop(); });
    main_thread_ = std::thread([this] { mainConnectLoop(); });

    printf("[IpcClient] 已启动，正在连接 QT(%s) 和 main(%s) 服务端...\n",
           SOCK_PATH_QT_DEVICE, SOCK_PATH_MAIN_DEVICE);
    return true;
}

// ================================================================
// stop：停止线程
// ----------------------------------------------------------------
// 关键：join 前必须先 shutdown 所有 fd，唤醒阻塞在 recv 上的线程
// ================================================================
void DeviceIpcClient::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    // 1. shutdown fd，唤醒阻塞在 recv/send 上的线程
    {
        std::lock_guard<std::mutex> lk(qt_fd_mutex_);
        if (qt_fd_ >= 0) shutdown(qt_fd_, SHUT_RDWR);
    }
    {
        std::lock_guard<std::mutex> lk(main_fd_mutex_);
        if (main_fd_ >= 0) shutdown(main_fd_, SHUT_RDWR);
    }

    // 2. join 线程（此时 recv 已被唤醒返回错误，线程会退出）
    if (qt_thread_.joinable())   qt_thread_.join();
    if (main_thread_.joinable()) main_thread_.join();
    printf("[IpcClient] 已停止\n");
}

// ================================================================
// qtConnectLoop：连接 QT 服务端的循环
// ================================================================
void DeviceIpcClient::qtConnectLoop()
{
    while (running_.load()) {
        int fd = ipc_sock_client_connect(SOCK_PATH_QT_DEVICE);
        if (fd < 0) {
            for (int i = 0; i < 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        printf("[IpcClient] 已连接 QT 服务端 fd=%d\n", fd);
        {
            std::lock_guard<std::mutex> lk(qt_fd_mutex_);
            qt_fd_ = fd;
        }
        handleQtConnection(fd);
        {
            std::lock_guard<std::mutex> lk(qt_fd_mutex_);
            qt_fd_ = -1;
        }
        printf("[IpcClient] QT 连接断开，准备重连...\n");
    }
}

// ================================================================
// mainConnectLoop：连接 main 服务端的循环
// ================================================================
void DeviceIpcClient::mainConnectLoop()
{
    while (running_.load()) {
        int fd = ipc_sock_client_connect(SOCK_PATH_MAIN_DEVICE);
        if (fd < 0) {
            for (int i = 0; i < 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        printf("[IpcClient] 已连接 main 服务端 fd=%d\n", fd);
        {
            std::lock_guard<std::mutex> lk(main_fd_mutex_);
            main_fd_ = fd;
        }
        handleMainConnection(fd);
        {
            std::lock_guard<std::mutex> lk(main_fd_mutex_);
            main_fd_ = -1;
        }
        printf("[IpcClient] main 连接断开，准备重连...\n");
    }
}

// ================================================================
// handleQtConnection：循环 recv DeviceCmd，回 DeviceStatusResp
// ----------------------------------------------------------------
// QT 发送 DeviceCmd（含 ESP32 相关命令）
// device 回 DeviceStatusResp / Esp32StatusResp
//
// ★ 消息分发：
//   - ADD_ESP32 / DEL_ESP32 / LIST_ESP32 用 Esp32StatusResp 回
//   - 其他用 DeviceStatusResp 回
// ================================================================
void DeviceIpcClient::handleQtConnection(int fd)
{
    while (running_.load()) {
        DeviceCmd cmd;
        memset(&cmd, 0, sizeof(cmd));

        ssize_t n = recv(fd, &cmd, sizeof(cmd), 0);
        if (n <= 0) {
            if (!running_.load()) {
            } else if (n == 0) {
                printf("[IpcClient] QT 服务端关闭 fd=%d\n", fd);
            } else {
                perror("[IpcClient] QT recv");
            }
            break;
        }
        if (n != sizeof(cmd)) {
            fprintf(stderr, "[IpcClient] QT 包长异常: %zd != %zu\n", n, sizeof(cmd));
            break;
        }

        switch (cmd.cmd) {
            case DEVICE_CMD_QUERY_ROOM: {
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                buildRoomResp(cmd.room_id, resp);
                send(fd, &resp, sizeof(resp), 0);
                break;
            }
            case DEVICE_CMD_LIST_ROOMS: {
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                buildRoomsResp(resp);
                send(fd, &resp, sizeof(resp), 0);
                break;
            }
            case DEVICE_CMD_ADD_PLUG: {
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                int rc = mgr_.addPlug(cmd.room_id, cmd.device_id,
                                      cmd.name, cmd.ip, cmd.token, 1);
                resp.count = (rc == 0) ? 0 : -1;
                send(fd, &resp, sizeof(resp), 0);
                printf("[IpcClient] ADD_PLUG 房间%d 设备%d %s -> %s\n",
                       cmd.room_id, cmd.device_id, cmd.name,
                       rc == 0 ? "OK" : "FAIL");
                break;
            }
            case DEVICE_CMD_DEL_PLUG: {
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                bool ok = mgr_.removePlug(cmd.room_id, cmd.device_id);
                resp.count = ok ? 0 : -1;
                send(fd, &resp, sizeof(resp), 0);
                printf("[IpcClient] DEL_PLUG 房间%d 设备%d -> %s\n",
                       cmd.room_id, cmd.device_id, ok ? "OK" : "FAIL");
                break;
            }
            case DEVICE_CMD_LIST_ESP32: {
                Esp32StatusResp resp;
                memset(&resp, 0, sizeof(resp));
                buildEsp32Resp(resp);
                send(fd, &resp, sizeof(resp), 0);
                break;
            }
            case DEVICE_CMD_ADD_ESP32: {
                Esp32StatusResp resp;
                memset(&resp, 0, sizeof(resp));
                int rc = esp32_.addEsp32(cmd.room_id, cmd.name, cmd.ip, cmd.rtsp_url);
                resp.count = (rc == 0) ? 0 : -1;
                send(fd, &resp, sizeof(resp), 0);
                printf("[IpcClient] ADD_ESP32 房间%d %s -> %s\n",
                       cmd.room_id, cmd.name, rc == 0 ? "OK" : "FAIL");
                break;
            }
            case DEVICE_CMD_DEL_ESP32: {
                Esp32StatusResp resp;
                memset(&resp, 0, sizeof(resp));
                bool ok = esp32_.removeEsp32(cmd.room_id);
                resp.count = ok ? 0 : -1;
                send(fd, &resp, sizeof(resp), 0);
                printf("[IpcClient] DEL_ESP32 房间%d -> %s\n",
                       cmd.room_id, ok ? "OK" : "FAIL");
                break;
            }
            case DEVICE_CMD_REGISTER: {
                // USB 主动登记：识别已成功，直接开插座+倒计时
                // 无插排时 onDeviceRegister 内部 findDevice 返回空自动跳过
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                bool ok = mgr_.onDeviceRegister(cmd.room_id, cmd.device_id,
                                                cmd.duration_minutes);
                resp.count = ok ? 0 : -1;
                send(fd, &resp, sizeof(resp), 0);
                printf("[IpcClient] REGISTER 房间%d 设备%d 时长%d分钟 -> %s\n",
                       cmd.room_id, cmd.device_id, cmd.duration_minutes,
                       ok ? "OK" : "FAIL(无插排或开失败)");
                break;
            }
            default:
                fprintf(stderr, "[IpcClient] 未知 QT 命令: %d\n", cmd.cmd);
                DeviceStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.count = -1;
                send(fd, &resp, sizeof(resp), 0);
        }
    }

    close(fd);
}

// ================================================================
// handleMainConnection：循环 recv，按 tag 区分消息类型
// ----------------------------------------------------------------
// ★★ Tag-based protocol (main → device):
//   先读 4 字节 tag，再按 tag 读对应大小的 payload。
//   彻底废弃 MSG_PEEK 按总长度匹配的方案——多消息合并时
//   MSG_PEEK 返回的 n 既不等于 16 也不等于 88，导致死循环。
//
//   tag=0 + DeviceEvent(16 bytes):           REGISTER / RELEASE
//   tag=1 + Esp32RecognizeResult(88 bytes):  识别结果回传
//
// ★ device → main 方向统一用 tag 前缀，防止 ack 与异步消息交错：
//   tag=0 + DeviceEvent:  ack（回应 main 的 DeviceEvent）
//   tag=1 + Esp32RecognizeEvent: ESP32 识别请求
//   tag=2 + DeviceEvent:  ESP32 终止/取消
//   tag=3 + int(0):       ack（回应 main 的 Esp32RecognizeResult）
// ================================================================
void DeviceIpcClient::handleMainConnection(int fd)
{
    while (running_.load()) {
        // ★ 先读 4 字节 tag（阻塞，MSG_WAITALL 确保读满）
        int tag = -1;
        ssize_t n = recv(fd, &tag, sizeof(tag), MSG_WAITALL);
        if (n <= 0) {
            if (!running_.load()) {
                // 正在关闭
            } else if (n == 0) {
                printf("[IpcClient] main 服务端关闭 fd=%d\n", fd);
            } else {
                perror("[IpcClient] main recv tag");
            }
            break;
        }
        if (n != (ssize_t)sizeof(tag)) {
            fprintf(stderr, "[IpcClient] main tag 不完整: %zd/%zu\n", n, sizeof(tag));
            break;
        }

        if (tag == 0) {
            // DeviceEvent (16 bytes)
            DeviceEvent ev;
            memset(&ev, 0, sizeof(ev));
            n = recv(fd, &ev, sizeof(ev), MSG_WAITALL);
            if (n != (ssize_t)sizeof(ev)) {
                fprintf(stderr, "[IpcClient] main DeviceEvent 不完整: %zd/%zu\n", n, sizeof(ev));
                break;
            }

            bool ok = false;
            switch (ev.event) {
                case DEVICE_EVENT_REGISTER:
                    // main 通知有设备需要登记 → 走 ESP32 被动登记流程
                    printf("[IpcClient] REGISTER 房间%d 设备%d 时长%d分钟 -> 触发ESP32识别\n",
                           ev.room_id, ev.device_id, ev.duration_minutes);
                    esp32_.onPassiveRegister(ev.room_id, ev.device_id, ev.duration_minutes);
                    ok = true;  // 异步流程，先回 OK 表示已接收
                    break;
                case DEVICE_EVENT_RELEASE:
                    // 设备释放：关插座 + 清计时
                    ok = mgr_.onDeviceRelease(ev.room_id, ev.device_id);
                    printf("[IpcClient] RELEASE 房间%d 设备%d -> %s\n",
                           ev.room_id, ev.device_id, ok ? "OK" : "FAIL");
                    break;
                default:
                    fprintf(stderr, "[IpcClient] 未知事件: %d\n", ev.event);
            }

            // ★ 回执：tag=0 + DeviceEvent ack（MSG_DONTWAIT 非阻塞）
            // ★ 合并 tag+payload 到单次 send，保证原子性，避免 main 端读到半条
            DeviceEvent ack;
            memset(&ack, 0, sizeof(ack));
            ack.event        = ok ? 0 : -1;
            ack.room_id      = ev.room_id;
            ack.device_id    = ev.device_id;
            ack.duration_minutes = ev.duration_minutes;
            {
                std::lock_guard<std::mutex> lk(main_fd_mutex_);
                struct { int tag; DeviceEvent ev; } pkt;
                pkt.tag = 0;
                pkt.ev  = ack;
                send(fd, &pkt, sizeof(pkt), MSG_DONTWAIT);
            }
        } else if (tag == 1) {
            // Esp32RecognizeResult (88 bytes)
            Esp32RecognizeResult result;
            memset(&result, 0, sizeof(result));
            n = recv(fd, &result, sizeof(result), MSG_WAITALL);
            if (n != (ssize_t)sizeof(result)) {
                fprintf(stderr, "[IpcClient] main Result 不完整: %zd/%zu\n", n, sizeof(result));
                break;
            }

            printf("[IpcClient] RECOGNIZE_RESULT task=%d success=%d user=%s\n",
                   result.task_id, result.success, result.person_name);

            // 交给 Esp32Manager 处理（开插座/写考勤/MQTT回ESP32）
            esp32_.onRecognizeResult(result);

            // ★ 回执：tag=3 + int(0)（MSG_DONTWAIT 非阻塞）
            // ★ 合并 tag+payload 到单次 send，保证原子性
            {
                std::lock_guard<std::mutex> lk(main_fd_mutex_);
                struct { int tag; int val; } pkt;
                pkt.tag = 3;
                pkt.val = 0;
                send(fd, &pkt, sizeof(pkt), MSG_DONTWAIT);
            }
        } else {
            fprintf(stderr, "[IpcClient] 未知 main tag=%d\n", tag);
        }
    }

    close(fd);
}

// ================================================================
// onEsp32Event：Esp32Manager 上抛事件 → 转发给 main_process
// ----------------------------------------------------------------
// 这是 device → main 方向的通讯
// ================================================================
void DeviceIpcClient::onEsp32Event(const Esp32EventMsg& msg)
{
    switch (msg.type) {
        case Esp32Event::kRecognizeReq:
            // ESP32 请求识别（被动 ready 或主动 req）→ 转发给 main
            sendEsp32RecognizeReq(msg);
            break;

        case Esp32Event::kRegisterAck: {
            // 识别完成。main 通过 Esp32RecognizeResult 已做状态机标记，
            // 但开插座是 device_process 的职责，这里补上。
            // 仅当登记任务识别成功时开插座，签到/签退不开插座。
            printf("[IpcClient] REGISTER_ACK task=%d type=%d success=%d 房间%d 设备%d\n",
                   msg.task_id, msg.task_type, msg.success, msg.room_id, msg.device_id);
            if (msg.success && msg.task_type == ESP32_TASK_REGISTER) {
                // 开插座 + 启动软件倒计时（无插排时 onDeviceRegister 内部 findDevice 返回空自动跳过）
                mgr_.onDeviceRegister(msg.room_id, msg.device_id, msg.duration_minutes);
                printf("[IpcClient] 已开插座 房间%d 设备%d 时长%d分钟\n",
                       msg.room_id, msg.device_id, msg.duration_minutes);
            }
            break;
        }

        case Esp32Event::kRelease:
            // ESP32 请求终止 → 通知 main 清检测状态 + 关插座
            printf("[IpcClient] ESP32 终止: 房间%d 设备%d → 通知 main RELEASE\n",
                   msg.room_id, msg.device_id);
            // 1. device_manager 关插座 + 清计时
            mgr_.onDeviceRelease(msg.room_id, msg.device_id);
            // 2. 通知 main 清检测状态（main 收到 RELEASE 会 ds_reset_device）
            sendDeviceEventToDevice(DEVICE_EVENT_RELEASE, msg.room_id,
                                    msg.device_id, 0, 1);
            break;

        case Esp32Event::kCancelRegister:
            // 30s 超时 → 通知 main 取消登记
            printf("[IpcClient] CANCEL_REGISTER task=%d 房间%d 设备%d → 通知 main\n",
                   msg.task_id, msg.room_id, msg.device_id);
            // 用 DeviceEvent event=4 表示 CANCEL
            sendDeviceEventToDevice(4, msg.room_id, msg.device_id, 0, 1);
            break;
    }
}

// ================================================================
// sendEsp32RecognizeReq：向 main 发送 Esp32RecognizeEvent
// ================================================================
bool DeviceIpcClient::sendEsp32RecognizeReq(const Esp32EventMsg& msg)
{
    std::lock_guard<std::mutex> lk(main_fd_mutex_);
    if (main_fd_ < 0) {
        fprintf(stderr, "[IpcClient] main 未连接，无法发送识别请求\n");
        return false;
    }

    Esp32RecognizeEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.task_id          = msg.task_id;
    ev.task_type        = msg.task_type;
    ev.room_id          = msg.room_id;
    ev.device_id        = msg.device_id;
    ev.duration_minutes = msg.duration_minutes;
    strncpy(ev.rtsp_url, msg.rtsp_url.c_str(), sizeof(ev.rtsp_url) - 1);

    // ★ 合并 tag+payload 到单次 send，保证原子性，避免 main 端读到半条
    struct { int tag; Esp32RecognizeEvent ev; } pkt;
    pkt.tag = 1;
    pkt.ev  = ev;
    if (send(main_fd_, &pkt, sizeof(pkt), MSG_DONTWAIT) != sizeof(pkt)) {
        perror("[IpcClient] send Esp32RecognizeEvent");
        return false;
    }

    printf("[IpcClient] 已发送识别请求: task=%d type=%d 房间%d 设备%d %s\n",
           msg.task_id, msg.task_type, msg.room_id, msg.device_id, msg.rtsp_url.c_str());
    return true;
}

// ================================================================
// sendDeviceEventToDevice：向 main 发送 DeviceEvent
// ================================================================
bool DeviceIpcClient::sendDeviceEventToDevice(int event, int room_id, int device_id,
                                               int duration, int success)
{
    std::lock_guard<std::mutex> lk(main_fd_mutex_);
    if (main_fd_ < 0) {
        fprintf(stderr, "[IpcClient] main 未连接，无法发送事件\n");
        return false;
    }

    // ★ 合并 tag+payload 到单次 send，保证原子性，避免 main 端读到半条
    DeviceEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event           = event;
    ev.room_id         = room_id;
    ev.device_id       = device_id;
    ev.duration_minutes= duration;

    struct { int tag; DeviceEvent ev; } pkt;
    pkt.tag = 2;
    pkt.ev  = ev;
    if (send(main_fd_, &pkt, sizeof(pkt), MSG_DONTWAIT) != sizeof(pkt)) {
        return false;
    }
    return true;
}

// ================================================================
// toItem：JSON 状态 → IPC DeviceStatusItem
// ================================================================
DeviceStatusItem DeviceIpcClient::toItem(const nlohmann::json& s)
{
    DeviceStatusItem item;
    memset(&item, 0, sizeof(item));

    item.room_id    = s.value("room_id", 0);
    item.device_id  = s.value("device_id", 0);

    std::string name = s.value("name", "?");
    strncpy(item.name, name.c_str(), sizeof(item.name) - 1);

    item.online     = s.value("online", false) ? 1 : 0;
    item.is_on      = s.value("is_on", false)  ? 1 : 0;
    item.fault      = s.value("fault", 0);
    item.temperature = s.value("temperature", 0);

    double pw = s.value("power_w", 0.0);
    item.power_w_x10 = (int)(pw * 10.0 + 0.5);

    double kwh = s.value("energy_kwh", 0.0);
    item.energy_kwh_x100 = (int)(kwh * 100.0 + 0.5);

    item.countdown_left_min = s.value("countdown_left_min", 0);
    item.on_off_count       = s.value("on_off_count", 0);

    return item;
}

// ================================================================
// buildRoomResp：构造指定房间的状态响应
// ================================================================
void DeviceIpcClient::buildRoomResp(int room_id, DeviceStatusResp& resp)
{
    auto arr = mgr_.getRoomStatus(room_id);
    int n = (int)arr.size();
    if (n > DEVICE_MAX_PER_ROOM) n = DEVICE_MAX_PER_ROOM;
    resp.count = n;
    for (int i = 0; i < n; ++i) {
        resp.items[i] = toItem(arr[i]);
    }
}

// ================================================================
// buildRoomsResp：构造房间列表响应
// ================================================================
void DeviceIpcClient::buildRoomsResp(DeviceStatusResp& resp)
{
    auto rooms = mgr_.getRoomsWithPlugs();
    int n = (int)rooms.size();
    if (n > DEVICE_MAX_PER_ROOM) n = DEVICE_MAX_PER_ROOM;
    resp.count = n;
    for (int i = 0; i < n; ++i) {
        memset(&resp.items[i], 0, sizeof(DeviceStatusItem));
        resp.items[i].room_id = rooms[i];
    }
}

// ================================================================
// buildEsp32Resp：构造 ESP32 列表响应
// ================================================================
void DeviceIpcClient::buildEsp32Resp(Esp32StatusResp& resp)
{
    Esp32StatusItem items[DEVICE_MAX_ESP32];
    int n = esp32_.listEsp32(items, DEVICE_MAX_ESP32);
    if (n < 0) n = 0;
    if (n > DEVICE_MAX_ESP32) n = DEVICE_MAX_ESP32;
    resp.count = n;
    for (int i = 0; i < n; ++i) {
        resp.items[i] = items[i];
    }
}
