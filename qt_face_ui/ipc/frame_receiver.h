#ifndef FRAME_RECEIVER_H
#define FRAME_RECEIVER_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <QVector>
#include <QMap>
#include <atomic>
#include "app/constants.h"

/**
 * @brief 帧接收器（独立线程）- 作为 Socket 服务器
 * 
 * 职责：
 * - 创建 Unix Socket 服务器，等待其他进程连接
 * - 接收 FrameMeta + DMA-BUF fd
 * - 解析检测框数据
 * - 通过信号通知 UI 更新
 */
class FrameReceiver : public QThread
{
    Q_OBJECT

public:
    explicit FrameReceiver(QObject *parent = nullptr);
    ~FrameReceiver();

    void startServer(const QString& socketPath);
    void stopServer();

signals:
    /**
     * @brief 新帧到达信号（包含 DMA-BUF fd）
     * @param stream_id 流ID（-1 表示 USB 摄像头）
     * @param fd        DMA-BUF 文件描述符
     * @param width     图像宽度
     * @param height    图像高度
     * @param size      数据大小
     * @param format    格式：0=NV12, 1=YUYV
     */
    void frameReady(int stream_id, int fd, int width, int height, int size, int format);

    /**
     * @brief 检测框更新信号
     * @param stream_id 流ID
     * @param boxes     检测框列表
     */
    void boxesReady(int stream_id, QVector<DetectionBox> boxes);

    /**
     * @brief 设备占用状态更新信号（主进程随帧下发）
     * @param streamId   流ID
     * @param deviceIds  该路设备编号列表
     * @param occupied   对应设备是否使用中（true=使用中/免打扰, false=空闲）
     *
     * Qt 端据此把 ROI 框渲染为：空闲=绿色，使用中=红色，并标注文字。
     */
    void deviceStatusReady(int streamId, QVector<int> deviceIds, QVector<bool> occupied);

protected:
    void run() override;

private:
    bool createServer();
    void waitForClient();
    void processFrame(const FrameMeta& meta, int dma_fd);

    QString             m_socketPath;
    std::atomic<bool>   m_running;
    int                 m_listenFd;
    int                 m_clientFd;

    struct StreamCache {
        int fd;
        int width;
        int height;
        int size;
        QVector<DetectionBox> boxes;
    };
    QVector<StreamCache> m_streamCaches;
    QMutex               m_mutex;
};

#endif