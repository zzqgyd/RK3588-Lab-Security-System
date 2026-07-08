#ifndef DEVICE_IPC_CLIENT_H
#define DEVICE_IPC_CLIENT_H

#include "device_manager.h"
#include "esp32_manager.h"
#include "ipc/ipc_socket.h"   // DeviceCmd / DeviceStatusItem / DeviceStatusResp / DeviceEvent（IPC 结构体）
#include <atomic>
#include <thread>
#include <mutex>

// ================================================================
// DeviceIpcClient：device_process 的 IPC 客户端
// ----------------------------------------------------------------
// 设备进程不创建任何服务端 socket，只作为客户端主动连接：
//
//   1. 连接 QT 服务端（SOCK_PATH_QT_DEVICE，QT 创建并监听）
//      - 长连接，device recv DeviceCmd → 处理 → send DeviceStatusResp
//      - 支持：查询房间状态 / 列出有插座的房间 / 新增删除插座
//              查询/新增/删除 ESP32
//
//   2. 连接 main 服务端（SOCK_PATH_MAIN_DEVICE，main 创建并监听）
//      - 长连接，双向通讯
//      - main → device: DeviceEvent (REGISTER/RELEASE)
//               Esp32RecognizeResult (识别结果回传)
//      - device → main: Esp32RecognizeEvent (请求拉流识别)
//                   DeviceEvent (RELEASE 转发，ESP32 终止)
//                   DeviceEvent (REGISTER_ACK，登记结果)
//
// 线程模型：
//   start() 启动两个连接线程，每条连接一个
//   断线自动重连（1 秒间隔）
// ================================================================
class DeviceIpcClient {
public:
    DeviceIpcClient(DeviceManager& mgr, Esp32Manager& esp32);
    ~DeviceIpcClient();

    // 禁拷贝
    DeviceIpcClient(const DeviceIpcClient&) = delete;
    DeviceIpcClient& operator=(const DeviceIpcClient&) = delete;

    // 启动两个连接线程（阻塞直到 stop）
    bool start();

    // 停止并关闭所有连接
    void stop();

private:
    // 连接 QT 服务端的循环（connect → recv 循环 → 断线重连）
    void qtConnectLoop();
    // 连接 main 服务端的循环（connect → recv 循环 → 断线重连）
    void mainConnectLoop();

    // 处理 QT 连接（循环 recv DeviceCmd，回 DeviceStatusResp）
    void handleQtConnection(int fd);
    // 处理 main 连接（循环 recv，区分 DeviceEvent / Esp32RecognizeResult）
    void handleMainConnection(int fd);

    // 把 JSON 状态转为 IPC 二进制结构
    static DeviceStatusItem toItem(const nlohmann::json& status);

    // 构造房间状态响应
    void buildRoomResp(int room_id, DeviceStatusResp& resp);
    // 构造房间列表响应
    void buildRoomsResp(DeviceStatusResp& resp);
    // 构造 ESP32 列表响应
    void buildEsp32Resp(Esp32StatusResp& resp);

    // Esp32Manager 事件回调（上抛的识别请求/终止/取消等）
    void onEsp32Event(const Esp32EventMsg& msg);

    // 向 main 发送 Esp32RecognizeEvent（识别请求）
    bool sendEsp32RecognizeReq(const Esp32EventMsg& msg);

    // 向 main 发送 DeviceEvent（RELEASE / REGISTER_ACK）
    bool sendDeviceEventToDevice(int event, int room_id, int device_id,
                                 int duration, int success);

private:
    DeviceManager& mgr_;
    Esp32Manager&  esp32_;
    std::atomic<bool> running_{false};
    std::thread qt_thread_;
    std::thread main_thread_;

    // 当前连接的 fd（stop 时需要 shutdown 唤醒阻塞的 recv）
    int             qt_fd_   = -1;
    std::mutex      qt_fd_mutex_;
    int             main_fd_ = -1;
    std::mutex      main_fd_mutex_;
};

#endif // DEVICE_IPC_CLIENT_H
