#include "face_ipc_server.h"
#include <QDebug>
#include <QSocketNotifier>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

// ================================================================
// 辅助函数：创建 Unix Socket 服务器
// ================================================================

/**
 * @brief 创建 Unix Socket 服务器
 * @param path socket 文件路径
 * @return socket 文件描述符，失败返回 -1
 */
static int createUnixServer(const char* path)
{
    // 删除已存在的 socket 文件
    unlink(path);
    
    // 创建 socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[FaceIpcServer] socket");
        return -1;
    }

    // 绑定地址
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[FaceIpcServer] bind");
        close(fd);
        return -1;
    }

    // 开始监听
    if (listen(fd, 5) < 0) {
        perror("[FaceIpcServer] listen");
        close(fd);
        return -1;
    }

    // 设置为非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    printf("[FaceIpcServer] Server listening on %s fd=%d\n", path, fd);
    return fd;
}

// ================================================================
// FaceIpcServer 实现
// ================================================================

FaceIpcServer::FaceIpcServer(QObject *parent)
    : QObject(parent)
    , m_listenFd(-1)
    , m_clientFd(-1)
    , m_clientConnected(false)
    , m_listenNotifier(nullptr)
    , m_clientNotifier(nullptr)
{
}

FaceIpcServer::~FaceIpcServer()
{
    stopServer();
}

bool FaceIpcServer::startServer(const QString& socketPath)
{
    if (m_listenFd >= 0) {
        stopServer();
    }

    m_listenFd = createUnixServer(socketPath.toUtf8().constData());
    if (m_listenFd < 0) {
        return false;
    }

    // 用 QSocketNotifier 监听 listen fd，事件驱动接受新连接
    m_listenNotifier = new QSocketNotifier(m_listenFd, QSocketNotifier::Read, this);
    connect(m_listenNotifier, &QSocketNotifier::activated,
            this, &FaceIpcServer::onListenActivity);

    qDebug() << "[FaceIpcServer] Server started on:" << socketPath;
    return true;
}

void FaceIpcServer::stopServer()
{
    closeClient();

    if (m_listenNotifier) {
        delete m_listenNotifier;
        m_listenNotifier = nullptr;
    }

    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }

    qDebug() << "[FaceIpcServer] Server stopped";
}

void FaceIpcServer::closeClient()
{
    if (m_clientNotifier) {
        delete m_clientNotifier;
        m_clientNotifier = nullptr;
    }

    if (m_clientFd >= 0) {
        close(m_clientFd);
        m_clientFd = -1;
    }
    m_clientConnected = false;
}

void FaceIpcServer::onListenActivity()
{
    // 监听 fd 可读 = 有新连接排队
    if (!m_clientConnected) {
        m_clientFd = accept(m_listenFd, NULL, NULL);
        if (m_clientFd >= 0) {
            // 设置为非阻塞模式
            int flags = fcntl(m_clientFd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(m_clientFd, F_SETFL, flags | O_NONBLOCK);
            }

            m_clientConnected = true;

            // 监听 client fd，事件驱动接收结果
            m_clientNotifier = new QSocketNotifier(m_clientFd, QSocketNotifier::Read, this);
            connect(m_clientNotifier, &QSocketNotifier::activated,
                    this, &FaceIpcServer::onClientActivity);

            emit clientConnected();
            qDebug() << "[FaceIpcServer] Client connected, fd=" << m_clientFd;
        }
    } else {
        // 已有连接，接受并立即关闭，避免积压
        int fd = accept(m_listenFd, NULL, NULL);
        if (fd >= 0) close(fd);
    }
}

void FaceIpcServer::onClientActivity()
{
    if (!m_clientConnected || m_clientFd < 0) {
        return;
    }

    FaceCommand cmd;
    ssize_t n = recv(m_clientFd, &cmd, sizeof(cmd), MSG_DONTWAIT);

    if (n == sizeof(cmd)) {
        // 收到结果
        emit commandResult(cmd.result, QString::fromUtf8(cmd.reply_name));
        qDebug() << "[FaceIpcServer] Received result:" << cmd.result
                 << "name:" << cmd.reply_name;
    } else if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // 读取错误，连接可能已断开
            qWarning() << "[FaceIpcServer] recv error:" << strerror(errno);
            closeClient();
            emit clientDisconnected();
        }
    } else if (n == 0) {
        // 连接关闭
        qDebug() << "[FaceIpcServer] Client disconnected";
        closeClient();
        emit clientDisconnected();
    }
}

void FaceIpcServer::sendCommand(const FaceCommand& cmd)
{
    if (!m_clientConnected || m_clientFd < 0) {
        qWarning() << "[FaceIpcServer] No client connected, cannot send command";
        return;
    }
    
    ssize_t n = send(m_clientFd, &cmd, sizeof(cmd), 0);
    if (n != sizeof(cmd)) {
        qWarning() << "[FaceIpcServer] Failed to send command";
        closeClient();
        emit clientDisconnected();
    } else {
        qDebug() << "[FaceIpcServer] Command sent, mode=" << cmd.mode;
    }
}

void FaceIpcServer::signIn()
{
    FaceCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.mode = 0;  // 签到
    sendCommand(cmd);
}

void FaceIpcServer::signOut()
{
    FaceCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.mode = 1;  // 签退
    sendCommand(cmd);
}

void FaceIpcServer::deviceRegister(int room, int device, int duration)
{
    FaceCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.mode = 2;  // 设备登记
    cmd.room_id = room;
    cmd.device_id = device;
    cmd.duration_minutes = duration;
    sendCommand(cmd);
}

void FaceIpcServer::faceEnroll(const QString& name, int roomId)
{
    FaceCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.mode = 3;  // 人脸录入
    cmd.room_id = roomId;
    strncpy(cmd.person_name, name.toUtf8().constData(), sizeof(cmd.person_name) - 1);
    cmd.person_name[sizeof(cmd.person_name) - 1] = '\0';
    sendCommand(cmd);
}

void FaceIpcServer::faceDelete(int featureId)
{
    qDebug() << "[FaceIpcServer] faceDelete called, featureId=" << featureId;
    
    FaceCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.mode = 4;
    cmd.feature_id = featureId;
    sendCommand(cmd);
}