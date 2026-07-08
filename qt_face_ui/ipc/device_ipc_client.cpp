#include "device_ipc_client.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <QDebug>

// ================================================================
// 辅助：连接到指定 socket 路径
// ================================================================
static int connectToPath(const char* path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// 辅助：设置接收超时
static void setRecvTimeout(int fd, int sec)
{
    struct timeval tv;
    tv.tv_sec  = sec;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// ================================================================
// DeviceIpcClient 实现
// ================================================================
DeviceIpcClient::DeviceIpcClient(QObject* parent)
    : QObject(parent), listen_fd_(-1) {
    device_fd_.store(-1);
}

DeviceIpcClient::~DeviceIpcClient()
{
    stopServer();
}

// ================================================================
// startServer：创建监听 socket，启动 accept 线程
// ================================================================
bool DeviceIpcClient::startServer()
{
    if (running_.exchange(true)) {
        return true;  // 已启动
    }

    listen_fd_ = ipc_sock_server_create(SOCK_PATH_QT_DEVICE, 5);
    if (listen_fd_ < 0) {
        qWarning() << "[DeviceIpc] Failed to create server socket:" << SOCK_PATH_QT_DEVICE;
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread([this] { acceptLoop(); });

    qDebug() << "[DeviceIpc] Server listening on" << SOCK_PATH_QT_DEVICE;
    return true;
}

// ================================================================
// stopServer：停止监听，关闭连接
// ----------------------------------------------------------------
// 注意：不能在这里获取 fd_mutex_！因为此时可能有一个查询线程
//       正持有 fd_mutex_ 阻塞在 recv() 上（1秒超时）。
//       直接 shutdown(fd) 会唤醒阻塞的 recv，让它返回错误，
//       sendCmdRecvResp 检测到错误后会释放 fd_mutex_。
// ================================================================
void DeviceIpcClient::stopServer()
{
    if (!running_.exchange(false)) {
        return;
    }

    // 1. shutdown listen fd，唤醒 acceptLoop 中的 accept()
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
    }

    // 2. shutdown device fd，唤醒阻塞在 recv/send 上的查询线程
    //    不加锁，因为我们要让阻塞的 recv 立即返回
    int dev_fd = device_fd_.load();
    if (dev_fd >= 0) {
        shutdown(dev_fd, SHUT_RDWR);
    }

    // 3. 等 accept 线程退出
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // 4. 此时 accept 已退出，查询线程也因 shutdown 返回错误退出
    //    可以安全关闭 fd
    {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        int fd = device_fd_.load();
        if (fd >= 0) {
            close(fd);
            device_fd_.store(-1);
        }
    }

    // 5. 关闭监听 socket
    if (listen_fd_ >= 0) {
        ipc_sock_close(listen_fd_, SOCK_PATH_QT_DEVICE);
        listen_fd_ = -1;
    }
}

// ================================================================
// acceptLoop：等待 device_process 连接
// ----------------------------------------------------------------
// device 连接后保存 fd，后续查询命令通过此 fd 发送
// 如果 device 断开重连，新连接替换旧 fd
// ================================================================
void DeviceIpcClient::acceptLoop()
{
    while (running_.load()) {
        int fd = ipc_sock_accept(listen_fd_);
        if (fd < 0) {
            if (!running_.load()) break;
            continue;
        }

        qDebug() << "[DeviceIpc] device_process connected fd=" << fd;

        std::lock_guard<std::mutex> lock(fd_mutex_);
        // 如果有旧连接，先关闭
        int old = device_fd_.load();
        if (old >= 0) {
            close(old);
        }
        device_fd_.store(fd);
    }
}

// ================================================================
// sendCmdRecvResp：通过 device 长连接发送命令并接收响应
// ----------------------------------------------------------------
// 互斥锁确保同一时间只有一个查询在进行（定时器刷新 + 手动刷新）
// ================================================================
bool DeviceIpcClient::sendCmdRecvResp(const DeviceCmd& cmd, DeviceStatusResp& resp)
{
    // 退出时快速失败，避免阻塞
    if (!running_.load()) return false;

    std::lock_guard<std::mutex> lock(fd_mutex_);

    int fd = device_fd_.load();
    if (fd < 0) {
        qWarning() << "[DeviceIpc] device_process 未连接";
        return false;
    }

    // 设置 1 秒接收超时
    setRecvTimeout(fd, 1);

    if (send(fd, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
        qWarning() << "[DeviceIpc] send cmd failed";
        close(fd);
        device_fd_.store(-1);
        return false;
    }

    memset(&resp, 0, sizeof(resp));
    ssize_t n = recv(fd, &resp, sizeof(resp), 0);
    if (n != sizeof(resp)) {
        qWarning() << "[DeviceIpc] recv resp failed, n=" << n;
        close(fd);
        device_fd_.store(-1);
        return false;
    }

    return true;
}

// ================================================================
// queryRoomStatus：查询指定房间所有插座状态
// ================================================================
bool DeviceIpcClient::queryRoomStatus(int room_id, QVector<DevicePlugStatus>& statuses)
{
    statuses.clear();

    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd     = DEVICE_CMD_QUERY_ROOM;
    cmd.room_id = room_id;

    DeviceStatusResp resp;
    if (!sendCmdRecvResp(cmd, resp)) {
        return false;
    }

    for (int i = 0; i < resp.count && i < DEVICE_MAX_PER_ROOM; ++i) {
        statuses.append(fromItem(resp.items[i]));
    }
    return true;
}

// ================================================================
// listRooms：查询哪些房间有插座
// ================================================================
bool DeviceIpcClient::listRooms(QVector<int>& rooms)
{
    rooms.clear();

    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = DEVICE_CMD_LIST_ROOMS;

    DeviceStatusResp resp;
    if (!sendCmdRecvResp(cmd, resp)) {
        return false;
    }

    for (int i = 0; i < resp.count && i < DEVICE_MAX_PER_ROOM; ++i) {
        rooms.append(resp.items[i].room_id);
    }
    return true;
}

// ================================================================
// addPlug：新增插座
// ================================================================
bool DeviceIpcClient::addPlug(int room_id, int device_id,
                              const QString& name, const QString& ip, const QString& token)
{
    // 去除前后空格（避免输入法误插入空格导致验证失败）
    QString nameT  = name.trimmed();
    QString ipT    = ip.trimmed();
    QString tokenT = token.trimmed();

    // 客户端预验证（给用户即时反馈，减少无效 IPC）
    if (nameT.isEmpty()) {
        qWarning() << "[DeviceIpc] 添加失败: 名称不能为空";
        return false;
    }
    if (tokenT.length() != 32) {
        qWarning() << "[DeviceIpc] 添加失败: token 长度应为 32，当前" << tokenT.length();
        return false;
    }

    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd       = DEVICE_CMD_ADD_PLUG;
    cmd.room_id   = room_id;
    cmd.device_id = device_id;

    // 转换为 UTF-8 后拷贝到固定长度字段
    QByteArray nameB  = nameT.toUtf8();
    QByteArray ipB    = ipT.toUtf8();
    QByteArray tokenB = tokenT.toUtf8();
    strncpy(cmd.name,  nameB.constData(),  sizeof(cmd.name)  - 1);
    strncpy(cmd.ip,    ipB.constData(),    sizeof(cmd.ip)    - 1);
    strncpy(cmd.token, tokenB.constData(), sizeof(cmd.token) - 1);

    DeviceStatusResp resp;
    if (!sendCmdRecvResp(cmd, resp)) {
        return false;
    }
    return resp.count == 0;  // 0=成功，-1=失败
}

// ================================================================
// removePlug：删除插座
// ================================================================
bool DeviceIpcClient::removePlug(int room_id, int device_id)
{
    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd       = DEVICE_CMD_DEL_PLUG;
    cmd.room_id   = room_id;
    cmd.device_id = device_id;

    DeviceStatusResp resp;
    if (!sendCmdRecvResp(cmd, resp)) {
        return false;
    }
    return resp.count == 0;
}

// ================================================================
// requestRegister：USB 主动登记使用（识别成功后开插座+倒计时）
// ----------------------------------------------------------------
// 通过 device 长连接直接发命令，不走 main 中转
// 返回 false 可能是无插排（正常，流程继续）或 IPC 失败
// ================================================================
bool DeviceIpcClient::requestRegister(int room_id, int device_id, int duration_minutes)
{
    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd             = DEVICE_CMD_REGISTER;
    cmd.room_id         = room_id;
    cmd.device_id       = device_id;
    cmd.duration_minutes = duration_minutes;

    DeviceStatusResp resp;
    if (!sendCmdRecvResp(cmd, resp)) {
        return false;
    }
    // count==0 表示有插排且已开；count==-1 表示无插排或开失败
    return resp.count == 0;
}

// ================================================================
// requestPowerOff：手动断电（经主进程中转）
// ----------------------------------------------------------------
// 这个走 SOCK_PATH_QT_MAIN，每次连接一次
// ================================================================
bool DeviceIpcClient::requestPowerOff(int room_id, int device_id)
{
    int fd = connectToMain();
    if (fd < 0) {
        qWarning() << "[DeviceIpc] connect main_process failed";
        return false;
    }
    setRecvTimeout(fd, 2);  // 主进程要转发给 device_process，给长一点

    MainCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd       = MAIN_CMD_POWER_OFF;
    cmd.room_id   = room_id;
    cmd.device_id = device_id;

    if (send(fd, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
        close(fd);
        return false;
    }

    MainAck ack;
    memset(&ack, 0, sizeof(ack));
    ssize_t n = recv(fd, &ack, sizeof(ack), 0);
    close(fd);

    if (n != sizeof(ack)) return false;
    return ack.result == 0;
}

// ================================================================
// sendCmdRecvEsp32Resp：发送命令并接收 Esp32StatusResp
// ----------------------------------------------------------------
// ESP32 相关命令（LIST/ADD/DEL）的响应是 Esp32StatusResp，
// 与插座命令的 DeviceStatusResp 不同，需单独处理
// ================================================================
bool DeviceIpcClient::sendCmdRecvEsp32Resp(const DeviceCmd& cmd, Esp32StatusResp& resp)
{
    if (!running_.load()) return false;

    std::lock_guard<std::mutex> lock(fd_mutex_);

    int fd = device_fd_.load();
    if (fd < 0) {
        qWarning() << "[DeviceIpc] device_process 未连接";
        return false;
    }

    setRecvTimeout(fd, 1);

    if (send(fd, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
        qWarning() << "[DeviceIpc] send esp32 cmd failed";
        close(fd);
        device_fd_.store(-1);
        return false;
    }

    memset(&resp, 0, sizeof(resp));
    ssize_t n = recv(fd, &resp, sizeof(resp), 0);
    if (n != sizeof(resp)) {
        qWarning() << "[DeviceIpc] recv esp32 resp failed, n=" << n;
        close(fd);
        device_fd_.store(-1);
        return false;
    }

    return true;
}

// ================================================================
// listEsp32：查询所有 ESP32 配置 + 在线状态
// ================================================================
bool DeviceIpcClient::listEsp32(QVector<Esp32Status>& statuses)
{
    statuses.clear();

    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = DEVICE_CMD_LIST_ESP32;

    Esp32StatusResp resp;
    if (!sendCmdRecvEsp32Resp(cmd, resp)) {
        return false;
    }

    for (int i = 0; i < resp.count && i < DEVICE_MAX_ESP32; ++i) {
        statuses.append(fromItem(resp.items[i]));
    }
    return true;
}

// ================================================================
// addEsp32：新增 ESP32
// ================================================================
bool DeviceIpcClient::addEsp32(int room_id, const QString& name,
                               const QString& ip, const QString& rtspUrl)
{
    QString nameT    = name.trimmed();
    QString ipT      = ip.trimmed();
    QString rtspT    = rtspUrl.trimmed();

    if (nameT.isEmpty()) {
        qWarning() << "[DeviceIpc] 添加ESP32失败: 名称不能为空";
        return false;
    }

    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd     = DEVICE_CMD_ADD_ESP32;
    cmd.room_id = room_id;

    QByteArray nameB  = nameT.toUtf8();
    QByteArray ipB    = ipT.toUtf8();
    QByteArray rtspB  = rtspT.toUtf8();
    strncpy(cmd.name,     nameB.constData(), sizeof(cmd.name)     - 1);
    strncpy(cmd.ip,       ipB.constData(),   sizeof(cmd.ip)       - 1);
    strncpy(cmd.rtsp_url, rtspB.constData(), sizeof(cmd.rtsp_url) - 1);

    Esp32StatusResp resp;
    if (!sendCmdRecvEsp32Resp(cmd, resp)) {
        return false;
    }
    return resp.count == 0;
}

// ================================================================
// removeEsp32：删除 ESP32
// ================================================================
bool DeviceIpcClient::removeEsp32(int room_id)
{
    DeviceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd     = DEVICE_CMD_DEL_ESP32;
    cmd.room_id = room_id;

    Esp32StatusResp resp;
    if (!sendCmdRecvEsp32Resp(cmd, resp)) {
        return false;
    }
    return resp.count == 0;
}

// ================================================================
// 私有辅助
// ================================================================
int DeviceIpcClient::connectToMain()
{
    return connectToPath(SOCK_PATH_QT_MAIN);
}

DevicePlugStatus DeviceIpcClient::fromItem(const DeviceStatusItem& item)
{
    DevicePlugStatus s;
    s.roomId          = item.room_id;
    s.deviceId        = item.device_id;
    s.name            = QString::fromUtf8(item.name);
    s.online          = item.online != 0;
    s.isOn            = item.is_on != 0;
    s.fault           = item.fault;
    s.temperature     = item.temperature;
    s.powerW          = item.power_w_x10 / 10.0;
    s.energyKwh       = item.energy_kwh_x100 / 100.0;
    s.countdownLeftMin = item.countdown_left_min;
    s.onOffCount      = item.on_off_count;

    switch (item.fault) {
        case 1:  s.faultDesc = "过温"; break;
        case 2:  s.faultDesc = "过载"; break;
        default: s.faultDesc = "无";
    }
    return s;
}

Esp32Status DeviceIpcClient::fromItem(const Esp32StatusItem& item)
{
    Esp32Status s;
    s.roomId   = item.room_id;
    s.name     = QString::fromUtf8(item.name);
    s.esp32Ip  = QString::fromUtf8(item.esp32_ip);
    s.rtspUrl  = QString::fromUtf8(item.rtsp_url);
    s.online   = item.online != 0;
    s.lastSeen = item.last_seen;
    return s;
}
