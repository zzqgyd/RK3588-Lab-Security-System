#include "app/application.h"
#include <QDebug>
#include <QTimer>
#include <signal.h>
#include <unistd.h>
#include <QMetaType>
#include <QApplication>
#include <QDir>

static void registerMetaTypes()
{
    qRegisterMetaType<DetectionBox>("DetectionBox");
    qRegisterMetaType<QVector<DetectionBox>>("QVector<DetectionBox>");
    qDebug() << "[Main] Meta types registered";
}

static FaceApplication* g_app = nullptr;

static void sigint_handler(int)
{
    qDebug() << "\n[Main] SIGINT received, shutting down...";
    if (g_app) {
        g_app->quit();
    }
}

int main(int argc, char *argv[])
{
    // ============================================================
    // 启用 tgtsml 输入法（Google 拼音）
    // ============================================================
    qputenv("QT_IM_MODULE", QByteArray("tgtsml"));
    
    // 词库路径：resources/dict
    QString dictPath = QDir::currentPath() + "../resources/dict";
    qputenv("TGTSML_DICT_PATH", dictPath.toUtf8());
    qDebug() << "[Main] Dict path:" << dictPath;
    
    // 注册元类型
    registerMetaTypes();
    
    // 创建应用程序
    FaceApplication app(argc, argv);
    g_app = &app;
    
    // 设置信号处理
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);   // 写已关闭的 socket 不杀进程（返回 EPIPE）
    
    // 初始化
    if (!app.initialize()) {
        qCritical() << "[Main] Failed to initialize application";
        return -1;
    }
    
    qDebug() << "[Main] Application started, waiting for events...";
    qDebug() << "[Main] Press Ctrl+C to exit";
    
    // 运行事件循环
    int ret = app.exec();
    
    qDebug() << "[Main] Application exited with code:" << ret;
    return 0;
}