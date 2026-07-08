#ifndef DEVICE_IPC_CLIENT_H
#define DEVICE_IPC_CLIENT_H

#include <QObject>
#include <QVector>
#include <QString>
#include <atomic>
#include <mutex>
#include <thread>
#include "ipc/ipc_socket.h"

// ================================================================
// DeviceIpcClient：Qt 端的设备管理 IPC
// ----------------------------------------------------------------
// 架构调整后（设备进程改为纯客户端）：
//
// 1. Qt 作为服务端（SOCK_PATH_QT_DEVICE）：
//    - startServer() 创建监听 socket
//    - device_process 启动后主动连接，保持长连接
//    - Qt 的查询/管理命令通过此长连接发送
//    - 支持：查询房间状态 / 列出房间 / 新增插座 / 删除插座
//
// 2. Qt 作为客户端（SOCK_PATH_QT_MAIN）：
//    - requestPowerOff() 每次连接 main_process 发送断电请求
//    - main 清状态后转发 RELEASE 给 device_process
//
// 线程模型：
//    - accept 线程：accept device 连接，保存 fd
//    - 查询方法在调用线程执行（send + recv），用 mutex 串行化
// ================================================================

// 单个插座的 UI 友好状态
struct DevicePlugStatus {
    int     roomId;
    int     deviceId;
    QString name;
    bool    online;
    bool    isOn;
    int     fault;            // 0/1/2
    QString faultDesc;
    int     temperature;      // ℃
    double  powerW;           // W
    double  energyKwh;        // KWh
    int     countdownLeftMin; // 倒计时剩余分钟
    int     onOffCount;
};

// 单个 ESP32 的 UI 友好状态
struct Esp32Status {
    int     roomId;
    QString name;
    QString esp32Ip;
    QString rtspUrl;
    bool    online;
    int64_t lastSeen;         // 最后心跳时间戳（秒）
};

class DeviceIpcClient : public QObject
{
    Q_OBJECT
public:
    explicit DeviceIpcClient(QObject* parent = nullptr);
    ~DeviceIpcClient();

    // ===== 服务端管理 =====

    // 创建 SOCK_PATH_QT_DEVICE 监听 socket，启动 accept 线程
    bool startServer();
    // 停止监听，关闭连接
    void stopServer();

    // ===== device_process IPC（通过长连接）=====

    // 查询指定房间所有插座状态
    bool queryRoomStatus(int room_id, QVector<DevicePlugStatus>& statuses);

    // 查询哪些房间有插座
    bool listRooms(QVector<int>& rooms);

    // 新增插座
    bool addPlug(int room_id, int device_id,
                 const QString& name, const QString& ip, const QString& token);

    // 删除插座
    bool removePlug(int room_id, int device_id);

    // 查询所有 ESP32 配置 + 在线状态
    bool listEsp32(QVector<Esp32Status>& statuses);

    // 新增 ESP32
    bool addEsp32(int room_id, const QString& name,
                  const QString& ip, const QString& rtspUrl);

    // 删除 ESP32
    bool removeEsp32(int room_id);

    // USB 主动登记使用：识别成功后开插座+倒计时（无插排时返回false，流程继续）
    bool requestRegister(int room_id, int device_id, int duration_minutes);

    // ===== main_process IPC（每次连接）=====

    // 手动断电（提前结束使用），主进程中转
    bool requestPowerOff(int room_id, int device_id);

private:
    // accept 线程：等待 device_process 连接
    void acceptLoop();

    // 通过 device 长连接发送命令并接收响应（带互斥锁）
    bool sendCmdRecvResp(const DeviceCmd& cmd, DeviceStatusResp& resp);
    // ESP32 命令专用（响应是 Esp32StatusResp）
    bool sendCmdRecvEsp32Resp(const DeviceCmd& cmd, Esp32StatusResp& resp);

    // 连接 main_process（每次调用连接一次）
    int  connectToMain();

    // 把 IPC 二进制结构转为 UI 结构
    static DevicePlugStatus fromItem(const DeviceStatusItem& item);
    static Esp32Status      fromItem(const Esp32StatusItem& item);

private:
    int              listen_fd_;     // 监听 fd（QT 服务端）
    std::atomic<int> device_fd_;     // device 连接的 fd（长连接，原子变量便于 stopServer 无锁读取）
    std::mutex       fd_mutex_;      // 保护 device_fd_ 的写访问（accept 替换、查询失败关闭）
    std::thread      accept_thread_;
    std::atomic<bool> running_{false};
};

#endif // DEVICE_IPC_CLIENT_H
