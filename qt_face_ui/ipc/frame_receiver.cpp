#include "frame_receiver.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <QDebug>
#include <cstring>
#include <errno.h>

#define CMSG_BUF_SIZE 1024

FrameReceiver::FrameReceiver(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , m_listenFd(-1)
    , m_clientFd(-1)
{
    m_streamCaches.resize(MAX_STREAMS);
    for (int i = 0; i < MAX_STREAMS; i++) {
        m_streamCaches[i].fd = -1;
        m_streamCaches[i].width = 0;
        m_streamCaches[i].height = 0;
        m_streamCaches[i].size = 0;
    }
}

FrameReceiver::~FrameReceiver()
{
    stopServer();
    wait();
}

void FrameReceiver::startServer(const QString& socketPath)
{
    m_socketPath = socketPath;
    m_running = true;
    start();
    qDebug() << "[FrameReceiver] Server started, socket:" << socketPath;
}

void FrameReceiver::stopServer()
{
    m_running = false;
    
    if (m_clientFd >= 0) {
        ::close(m_clientFd);
        m_clientFd = -1;
    }
    
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    
    unlink(m_socketPath.toUtf8().constData());
    
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (m_streamCaches[i].fd >= 0) {
            ::close(m_streamCaches[i].fd);
            m_streamCaches[i].fd = -1;
        }
    }
    
    qDebug() << "[FrameReceiver] Server stopped";
}

bool FrameReceiver::createServer()
{
    unlink(m_socketPath.toUtf8().constData());
    
    m_listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        perror("[FrameReceiver] socket");
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socketPath.toUtf8().constData(), sizeof(addr.sun_path) - 1);
    
    if (::bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[FrameReceiver] bind");
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    
    if (listen(m_listenFd, 5) < 0) {
        perror("[FrameReceiver] listen");
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    
    qDebug() << "[FrameReceiver] Server created, listening on" << m_socketPath;
    return true;
}

void FrameReceiver::waitForClient()
{
    qDebug() << "[FrameReceiver] Waiting for client connection...";
    
    int flags = fcntl(m_listenFd, F_GETFL, 0);
    if (flags >= 0 && !(flags & O_NONBLOCK)) {
        fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);
    }
    
    while (m_running && m_clientFd < 0) {
        m_clientFd = accept(m_listenFd, NULL, NULL);
        if (m_clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                for (int i = 0; i < 10 && m_running; i++) {
                    msleep(10);
                }
                continue;
            }
            perror("[FrameReceiver] accept");
            break;
        }
    }
    
    if (flags >= 0) {
        fcntl(m_listenFd, F_SETFL, flags);
    }
    
    if (m_clientFd >= 0) {
        qDebug() << "[FrameReceiver] Client connected, fd=" << m_clientFd;
    }
}

void FrameReceiver::processFrame(const FrameMeta& meta, int dma_fd)
{
    int stream_id = meta.stream_id;
    
    if (stream_id < -1 || stream_id >= MAX_STREAMS) {
        if (dma_fd >= 0) ::close(dma_fd);
        return;
    }
    
    // ============================================================
    // 确定格式：stream_id == -1 表示 USB 摄像头（YUYV 格式）
    // 否则为主进程监控画面（NV12 格式）
    // ============================================================
    int format = (stream_id == -1) ? 1 : 0;
    
    QVector<DetectionBox> boxesCopy;
    for (uint32_t i = 0; i < meta.detection_count && i < 64; i++) {
        boxesCopy.append(meta.boxes[i]);
    }
    
    if (stream_id >= 0) {
        QMutexLocker locker(&m_mutex);
        
        if (m_streamCaches[stream_id].fd >= 0) {
            ::close(m_streamCaches[stream_id].fd);
        }
        
        m_streamCaches[stream_id].fd = (dma_fd >= 0) ? dup(dma_fd) : -1;
        m_streamCaches[stream_id].width = meta.width;
        m_streamCaches[stream_id].height = meta.height;
        m_streamCaches[stream_id].size = meta.size;
        m_streamCaches[stream_id].boxes = boxesCopy;
    }
    
    // 发送帧信号（带 format 参数）
    if (dma_fd >= 0) {
        int send_fd = dup(dma_fd);
        emit frameReady(stream_id, send_fd, meta.width, meta.height, meta.size, format);
    }
    
    if (meta.detection_count > 0) {
        emit boxesReady(stream_id, boxesCopy);
    }

    // 设备占用状态（主进程随帧下发）
    if (stream_id >= 0 && meta.device_count > 0) {
        QVector<int>  ids;
        QVector<bool> occ;
        for (uint32_t i = 0; i < meta.device_count && i < (uint32_t)ROI_MAX_DEVICES; i++) {
            ids.append(meta.device_ids[i]);
            occ.append(meta.device_occupied[i] != 0);
        }
        emit deviceStatusReady(stream_id, ids, occ);
    }

    if (dma_fd >= 0) {
        ::close(dma_fd);
    }
}

void FrameReceiver::run()
{
    if (!createServer()) {
        qWarning() << "[FrameReceiver] Failed to create server";
        return;
    }
    
    while (m_running) {
        if (m_clientFd < 0) {
            waitForClient();
            if (m_clientFd < 0 && m_running) {
                continue;
            }
            if (!m_running) break;
        }
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        setsockopt(m_clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        FrameMeta meta;
        char cmsg_buf[CMSG_BUF_SIZE];
        struct iovec iov;
        struct msghdr msg;
        
        memset(&msg, 0, sizeof(msg));
        iov.iov_base = &meta;
        iov.iov_len = sizeof(meta);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        
        ssize_t n = recvmsg(m_clientFd, &msg, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            qDebug() << "[FrameReceiver] Client disconnected";
            ::close(m_clientFd);
            m_clientFd = -1;
            continue;
        }
        
        int dma_fd = -1;
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            memcpy(&dma_fd, CMSG_DATA(cmsg), sizeof(dma_fd));
        }
        
        if (n != sizeof(meta)) {
            qWarning() << "[FrameReceiver] Invalid meta size:" << n;
            if (dma_fd >= 0) ::close(dma_fd);
            continue;
        }
        
        processFrame(meta, dma_fd);
    }
}