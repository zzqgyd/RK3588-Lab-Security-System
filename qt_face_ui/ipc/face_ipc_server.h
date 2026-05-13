#ifndef FACE_IPC_SERVER_H
#define FACE_IPC_SERVER_H

#include <QObject>
#include "app/constants.h"

class QSocketNotifier;

/**
 * @brief 人脸进程 IPC 服务器（QT 端）
 *
 * 职责：
 * - 创建 Unix Socket 服务器 (sock_qt_face)
 * - 等待人脸进程连接（人脸进程作为客户端）
 * - 发送命令（签到/签退/设备登记/录入/删除）
 * - 接收人脸进程返回的结果
 *
 * 使用方式：
 *   1. 创建 FaceIpcServer 实例
 *   2. 调用 startServer() 启动服务器
 *   3. 通过信号 commandResult 接收结果
 *   4. 调用 signIn/signOut 等方法发送命令
 *
 * 说明：使用 QSocketNotifier 事件驱动，无需外部轮询
 */
class FaceIpcServer : public QObject
{
    Q_OBJECT

public:
    explicit FaceIpcServer(QObject *parent = nullptr);
    ~FaceIpcServer();

    /**
     * @brief 启动服务器
     * @param socketPath Socket 文件路径，默认 SOCK_PATH_QT_FACE
     * @return true 成功, false 失败
     */
    bool startServer(const QString& socketPath = SOCK_PATH_QT_FACE);

    /**
     * @brief 停止服务器，关闭所有连接
     */
    void stopServer();

    // ============================================================
    // 命令发送接口
    // ============================================================

    /**
     * @brief 签到
     */
    void signIn();

    /**
     * @brief 签退
     */
    void signOut();

    /**
     * @brief 设备登记
     * @param room      房间号 (0-7)
     * @param device    设备号 (1-4)
     * @param duration  使用时长（分钟）
     */
    void deviceRegister(int room, int device, int duration);

    /**
     * @brief 人脸录入
     * @param name 姓名
     * @param roomId 所属房间号
     */
    void faceEnroll(const QString& name, int roomId);

    /**
     * @brief 人脸删除
     * @param featureId 特征ID
     */
    void faceDelete(int featureId);

signals:
    /**
     * @brief 命令结果信号
     * @param result    0成功 -1失败 -2超时 -3重复录入 -5今日已签到/签退
     * @param replyName 返回的人名
     */
    void commandResult(int result, const QString& replyName);

    /**
     * @brief 客户端连接状态变化
     */
    void clientConnected();
    void clientDisconnected();

private slots:
    // QSocketNotifier 回调（事件驱动，替代外部轮询）
    void onListenActivity();   // 监听 fd 可读 → 有新连接
    void onClientActivity();   // 客户端 fd 可读 → 有结果或断开

private:
    /**
     * @brief 发送命令到人脸进程
     */
    void sendCommand(const FaceCommand& cmd);

    /**
     * @brief 关闭客户端连接
     */
    void closeClient();

    int  m_listenFd;        // 监听 socket 文件描述符
    int  m_clientFd;        // 客户端连接 socket 文件描述符
    bool m_clientConnected; // 客户端是否已连接

    QSocketNotifier* m_listenNotifier;  // 监听 listen fd 的通知器
    QSocketNotifier* m_clientNotifier;  // 监听 client fd 的通知器
};

#endif
