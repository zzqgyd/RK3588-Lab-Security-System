#include "application.h"
#include "windows/hdmi_window.h"
#include "windows/dsi_window.h"
#include "windows/face_list_window.h"
#include "windows/roi_config_window.h"
#include "ipc/frame_receiver.h"
#include "ipc/face_ipc_server.h"
#include "db/db_manager.h"
#include <QScreen>
#include <QDebug>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

// 辅助函数：屏幕管理
static QScreen* getPrimaryScreen()
{
    QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) return nullptr;
    for (QScreen* screen : screens) {
        if (screen->geometry().x() == 0 && screen->geometry().y() == 0) {
            return screen;
        }
    }
    return screens[0];
}

static QScreen* getSecondaryScreen()
{
    QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = getPrimaryScreen();
    for (QScreen* screen : screens) {
        if (screen != primary) return screen;
    }
    return nullptr;
}

static void moveWindowToScreen(QWidget* window, QScreen* targetScreen)
{
    if (!window || !targetScreen) return;
    window->move(targetScreen->geometry().x(), targetScreen->geometry().y());
}

// ================================================================
// FaceApplication 实现
// ================================================================

FaceApplication::FaceApplication(int &argc, char **argv)
    : QApplication(argc, argv)
    , m_hdmiWindow(nullptr)
    , m_dsiWindow(nullptr)
    , m_mainFrameReceiver(nullptr)
    , m_usbFrameReceiver(nullptr)
    , m_faceCommandServer(nullptr)
    , m_faceListPage(nullptr)
    , m_roiConfigPage(nullptr)
    , m_showingUsb(false)
    , m_usbHideTimer(nullptr)
{
    setApplicationName("Lab");
    setOrganizationName("DeviceMonitor");
}

FaceApplication::~FaceApplication()
{
    if (m_usbHideTimer) delete m_usbHideTimer;
    if (m_faceCommandServer) {
        m_faceCommandServer->stopServer();
        delete m_faceCommandServer;
    }
    if (m_mainFrameReceiver) delete m_mainFrameReceiver;
    if (m_usbFrameReceiver) delete m_usbFrameReceiver;
    if (m_hdmiWindow) delete m_hdmiWindow;
    if (m_dsiWindow) delete m_dsiWindow;
    if (m_roiConfigPage) delete m_roiConfigPage;
    qDebug() << "[App] Shutdown";
}

bool FaceApplication::initialize()
{
    qDebug() << "[App] Initializing...";
    
    DbManager::instance().init();
    
    QList<QScreen*> screens = QGuiApplication::screens();
    qDebug() << "[App] Found" << screens.size() << "screens:";
    for (int i = 0; i < screens.size(); i++) {
        qDebug() << "  Screen" << i << ":" << screens[i]->name() << screens[i]->geometry();
    }
    
    setupWindows();
    setupFrameReceivers();
    setupCommandServer();
    connectDsiSignals();
    connectFaceListSignals();
    setupRoiConfigPage();
    
    m_usbHideTimer = new QTimer(this);
    m_usbHideTimer->setSingleShot(true);
    connect(m_usbHideTimer, &QTimer::timeout, [this]() {
        if (m_dsiWindow) {
            m_dsiWindow->hideUsbCamera();
            m_showingUsb = false;
        }
    });
    
    qDebug() << "[App] Initialization complete";
    return true;
}

void FaceApplication::setupWindows()
{
    QScreen* hdmiScreen = getPrimaryScreen();
    QScreen* dsiScreen = getSecondaryScreen();
    
    m_hdmiWindow = new HDMIMainWindow();
    if (hdmiScreen) {
        moveWindowToScreen(m_hdmiWindow, hdmiScreen);
        m_hdmiWindow->resize(hdmiScreen->size());
    }
    m_hdmiWindow->showFullScreen();
    
    m_dsiWindow = new DSIMainWindow();
    if (dsiScreen) {
        moveWindowToScreen(m_dsiWindow, dsiScreen);
        m_dsiWindow->resize(dsiScreen->size());
    }
    m_dsiWindow->showFullScreen();
    
    m_faceListPage = new FaceListWindow();
}

void FaceApplication::setupFrameReceivers()
{
    // 主进程帧接收器（监控画面，NV12 格式）
    m_mainFrameReceiver = new FrameReceiver(this);
    connect(m_mainFrameReceiver, &FrameReceiver::frameReady,
            this, &FaceApplication::onMainFrameReady);
    connect(m_mainFrameReceiver, &FrameReceiver::boxesReady,
            this, &FaceApplication::onMainBoxesReady);
    connect(m_mainFrameReceiver, &FrameReceiver::deviceStatusReady,
            this, &FaceApplication::onMainDeviceStatusReady);
    m_mainFrameReceiver->startServer(SOCK_PATH_MAIN_QT);
    
    // USB 摄像头帧接收器（人脸识别画面，YUYV 格式）
    m_usbFrameReceiver = new FrameReceiver(this);
    connect(m_usbFrameReceiver, &FrameReceiver::frameReady, 
            this, &FaceApplication::onUsbFrameReady);
    m_usbFrameReceiver->startServer(SOCK_PATH_FACE_QT);
    
    qDebug() << "[App] Frame receivers started";
}

void FaceApplication::setupCommandServer()
{
    m_faceCommandServer = new FaceIpcServer(this);
    connect(m_faceCommandServer, &FaceIpcServer::commandResult,
            this, &FaceApplication::onCommandResult);
    connect(m_faceCommandServer, &FaceIpcServer::clientConnected,
            this, &FaceApplication::onFaceClientConnected);

    if (!m_faceCommandServer->startServer(SOCK_PATH_QT_FACE)) {
        qWarning() << "[App] Failed to start command server";
    }
    // 说明：连接/收发由 QSocketNotifier 事件驱动，无需定时器轮询
}

void FaceApplication::connectDsiSignals()
{
    if (!m_dsiWindow) return;
    
    connect(m_dsiWindow, &DSIMainWindow::switchToFullscreen,
            this, &FaceApplication::onSwitchToFullscreen);
    connect(m_dsiWindow, &DSIMainWindow::switchToGrid,
            this, &FaceApplication::onSwitchToGrid);
    connect(m_dsiWindow, &DSIMainWindow::toggleShowBoxes,
            this, &FaceApplication::onToggleShowBoxes);
    connect(m_dsiWindow, &DSIMainWindow::toggleShowRois,
            this, &FaceApplication::onToggleShowRois);
    connect(m_dsiWindow, &DSIMainWindow::roiConfigRequested,
            this, &FaceApplication::onRoiConfigRequested);
    connect(m_dsiWindow, &DSIMainWindow::signInRequested,
            this, &FaceApplication::onSignInRequested);
    connect(m_dsiWindow, &DSIMainWindow::signOutRequested,
            this, &FaceApplication::onSignOutRequested);
    connect(m_dsiWindow, &DSIMainWindow::deviceRegisterRequested,
            this, &FaceApplication::onDeviceRegisterRequested);
    connect(m_dsiWindow, &DSIMainWindow::faceEnrollRequested,
            this, &FaceApplication::onFaceEnrollRequested);
    connect(m_dsiWindow, &DSIMainWindow::faceDeleteRequested,
            this, &FaceApplication::onFaceDeleteRequested);
}

void FaceApplication::connectFaceListSignals()
{
    qDebug() << "[App] Face delete signal will be forwarded via DSI window";
}

// ================================================================
// ROI 配置页面：作为 DSI 屏上的独立顶层窗口（复用触摸屏）
// ================================================================
void FaceApplication::setupRoiConfigPage()
{
    // 用 nullptr 作为父对象，使其成为独立顶层窗口
    m_roiConfigPage = new RoiConfigWindow(nullptr);

    // ROI 保存后：刷新 HDMI 显示 + 通过 socket 通知主进程热重载
    connect(m_roiConfigPage, &RoiConfigWindow::roiSaved, this, &FaceApplication::onRoiSaved);
    // 返回主页
    connect(m_roiConfigPage, &RoiConfigWindow::backToHome, this, &FaceApplication::onRoiBackToHome);

    // 放到 DSI 屏上
    QScreen* dsiScreen = getSecondaryScreen();
    if (!dsiScreen) dsiScreen = getPrimaryScreen();
    if (dsiScreen) {
        m_roiConfigPage->move(dsiScreen->geometry().x(), dsiScreen->geometry().y());
        m_roiConfigPage->resize(dsiScreen->size());
    }

    qDebug() << "[App] ROI config page set up";
}

// ================================================================
// 通过 Unix Socket 通知主进程重载 ROI（发送 'R'，等待 'O' 确认）
// ================================================================
static void notifyMainProcessReloadRoi()
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH_MAIN_ROI, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        qWarning() << "[App] connect main roi socket failed:" << SOCK_PATH_MAIN_ROI;
        close(fd);
        return;
    }

    char cmd = 'R';
    if (send(fd, &cmd, 1, 0) != 1) {
        qWarning() << "[App] send roi reload cmd failed";
        close(fd);
        return;
    }

    // 等待主进程确认（最多 500ms）
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char ack = 0;
    ssize_t n = recv(fd, &ack, 1, 0);
    if (n == 1 && ack == 'O') {
        qDebug() << "[App] Main process reloaded ROI (acked)";
    } else {
        qWarning() << "[App] ROI reload ack not received (n=" << n << ")";
    }
    close(fd);
}

// ================================================================
// 帧接收回调
// ================================================================

void FaceApplication::onMainFrameReady(int stream_id, int fd, int width, int height, int size, int format)
{
    // 主进程画面是 NV12 格式，format 参数忽略，直接传给 HDMI 窗口
    // 注意：fd 只能用一次，HDMI 窗口内部会 dup；若 ROI 配置窗口可见，
    //       需要同一帧也送过去，因此先送 ROI（dup 后用），再送 HDMI（消费原 fd）。
    if (m_roiConfigPage && m_roiConfigPage->isVisible()) {
        // ROI 窗口内部会 dup fd，不会真正关闭原 fd
        m_roiConfigPage->updateFrame(stream_id, fd, width, height, size, format);
    }

    if (m_hdmiWindow) {
        m_hdmiWindow->updateFrame(stream_id, fd, width, height, size);
    } else if (fd >= 0) {
        ::close(fd);
    }
}

void FaceApplication::onMainBoxesReady(int stream_id, QVector<DetectionBox> boxes)
{
    if (m_hdmiWindow) {
        m_hdmiWindow->updateBoxes(stream_id, boxes);
    }
}

void FaceApplication::onMainDeviceStatusReady(int stream_id, QVector<int> deviceIds, QVector<bool> occupied)
{
    if (m_hdmiWindow) {
        m_hdmiWindow->updateDeviceStatus(stream_id, deviceIds, occupied);
    }
}

void FaceApplication::onUsbFrameReady(int stream_id, int fd, int width, int height, int size, int format)
{
    if (m_showingUsb && m_dsiWindow) {
        m_dsiWindow->updateUsbFrame(fd, width, height, size, format);
    } else if (fd >= 0) {
        ::close(fd);
    }
}

// ================================================================
// 命令服务器回调
// ================================================================

void FaceApplication::onCommandResult(int result, const QString& replyName)
{
    m_showingUsb = false;
    m_usbHideTimer->stop();
    
    QString msg, title;
    switch (result) {
        case 0:   title = "成功"; msg = QString("识别成功\n%1").arg(replyName); break;
        case -1:  title = "失败"; msg = "识别失败"; break;
        case -2:  title = "超时"; msg = "识别超时"; break;
        case -3:  title = "重复"; msg = QString("重复录入\n%1").arg(replyName); break;
        case -5:  title = "提示"; msg = QString("%1今日已操作").arg(replyName); break;
        default:  title = "错误"; msg = QString("未知错误: %1").arg(result); break;
    }
    
    QMessageBox::information(m_dsiWindow, title, msg);
    
    if (m_dsiWindow) {
        m_dsiWindow->hideUsbCamera();
    }
}

void FaceApplication::onFaceClientConnected()
{
    qDebug() << "[App] Face client connected";
}

// ================================================================
// 显示控制回调
// ================================================================

void FaceApplication::onSwitchToFullscreen(int stream_id)
{
    if (m_hdmiWindow) m_hdmiWindow->switchToFullscreenMode(stream_id);
}

void FaceApplication::onSwitchToGrid()
{
    if (m_hdmiWindow) m_hdmiWindow->switchToGridMode();
}

void FaceApplication::onToggleShowBoxes(bool show)
{
    if (m_hdmiWindow) m_hdmiWindow->setShowBoxes(show);
}

void FaceApplication::onToggleShowRois(bool show)
{
    if (m_hdmiWindow) m_hdmiWindow->setShowRois(show);
}

// 点击"ROI配置"按钮：在 DSI 屏上弹出 ROI 配置窗口
void FaceApplication::onRoiConfigRequested()
{
    qDebug() << "[App] ROI config requested";
    if (!m_roiConfigPage) return;
    // 放到 DSI 屏并全屏显示
    QScreen* dsiScreen = getSecondaryScreen();
    if (!dsiScreen) dsiScreen = getPrimaryScreen();
    if (dsiScreen) {
        m_roiConfigPage->move(dsiScreen->geometry().x(), dsiScreen->geometry().y());
        m_roiConfigPage->resize(dsiScreen->size());
    }
    m_roiConfigPage->showFullScreen();
    m_roiConfigPage->raise();
    m_roiConfigPage->activateWindow();
}

// ROI 配置保存后：刷新 HDMI 的 ROI 显示 + 通知主进程热重载
void FaceApplication::onRoiSaved()
{
    qDebug() << "[App] ROI saved, reloading HDMI and notifying main process";
    if (m_hdmiWindow) {
        m_hdmiWindow->reloadRoiConfig();
    }
    notifyMainProcessReloadRoi();
}

// ROI 配置窗口返回主页
void FaceApplication::onRoiBackToHome()
{
    if (m_roiConfigPage) {
        m_roiConfigPage->hide();
    }
}

// ================================================================
// 人脸操作回调
// ================================================================

void FaceApplication::onSignInRequested()
{
    qDebug() << "[App] Sign in requested";
    if (m_dsiWindow) {
        m_dsiWindow->showUsbCamera();
        m_showingUsb = true;
    }
    if (m_faceCommandServer) {
        m_faceCommandServer->signIn();
    }
}

void FaceApplication::onSignOutRequested()
{
    qDebug() << "[App] Sign out requested";
    if (m_dsiWindow) {
        m_dsiWindow->showUsbCamera();
        m_showingUsb = true;
    }
    if (m_faceCommandServer) {
        m_faceCommandServer->signOut();
    }
}

void FaceApplication::onDeviceRegisterRequested()
{
    qDebug() << "[App] Device register requested";
    
    // ★ 改用非模态方式，分步输入
    QInputDialog* dialog = new QInputDialog(m_dsiWindow);
    dialog->setWindowTitle("设备登记 - 房间号");
    dialog->setLabelText("请输入房间号 (0-7):");
    dialog->setInputMode(QInputDialog::IntInput);
    dialog->setIntRange(0, 7);
    dialog->setIntValue(0);
    dialog->setIntStep(1);
    dialog->setModal(false);                    // ★ 非模态
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(dialog, &QInputDialog::accepted, [this, dialog]() {
        int room = dialog->intValue();
        
        // 第二步：设备号
        QInputDialog* dialog2 = new QInputDialog(m_dsiWindow);
        dialog2->setWindowTitle("设备登记 - 设备号");
        dialog2->setLabelText("请输入设备号 (1-4):");
        dialog2->setInputMode(QInputDialog::IntInput);
        dialog2->setIntRange(1, 4);
        dialog2->setIntValue(1);
        dialog2->setIntStep(1);
        dialog2->setModal(false);
        dialog2->setAttribute(Qt::WA_DeleteOnClose);
        
        connect(dialog2, &QInputDialog::accepted, [this, dialog2, room]() {
            int device = dialog2->intValue();
            
            // 第三步：使用时长
            QInputDialog* dialog3 = new QInputDialog(m_dsiWindow);
            dialog3->setWindowTitle("设备登记 - 时长");
            dialog3->setLabelText("使用时长(分钟):");
            dialog3->setInputMode(QInputDialog::IntInput);
            dialog3->setIntRange(1, 120);
            dialog3->setIntValue(15);
            dialog3->setIntStep(5);
            dialog3->setModal(false);
            dialog3->setAttribute(Qt::WA_DeleteOnClose);
            
            connect(dialog3, &QInputDialog::accepted, [this, dialog3, room, device]() {
                int duration = dialog3->intValue();
                
                if (m_dsiWindow) {
                    m_dsiWindow->showUsbCamera();
                    m_showingUsb = true;
                }
                if (m_faceCommandServer) {
                    m_faceCommandServer->deviceRegister(room, device, duration);
                }
            });
            
            dialog3->show();
        });
        
        dialog2->show();
    });
    
    dialog->show();
}

void FaceApplication::onFaceEnrollRequested()
{
    qDebug() << "[App] Face enroll requested";

    // 自定义对话框：姓名 + 房间号选择
    QDialog* dialog = new QDialog(m_dsiWindow);
    dialog->setWindowTitle("人脸录入");
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QFormLayout* form = new QFormLayout(dialog);

    QLineEdit* nameEdit = new QLineEdit(dialog);
    nameEdit->setPlaceholderText("请输入姓名");
    QComboBox* roomCombo = new QComboBox(dialog);
    for (int i = 1; i <= 8; i++) {
        roomCombo->addItem(QString("房间%1").arg(i), i - 1);  // 显示1-8，对应 room_id 0-7
    }
    roomCombo->setCurrentIndex(0);

    form->addRow("姓名:", nameEdit);
    form->addRow("房间:", roomCombo);

    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, dialog);
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    connect(dialog, &QDialog::accepted, [this, nameEdit, roomCombo]() {
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) return;
        int roomId = roomCombo->currentData().toInt();

        if (m_dsiWindow) {
            m_dsiWindow->showUsbCamera();
            m_showingUsb = true;
        }
        if (m_faceCommandServer) {
            m_faceCommandServer->faceEnroll(name, roomId);
        }
    });

    dialog->show();
}

void FaceApplication::onFaceDeleteRequested(int featureId, const QString& name)
{
    qDebug() << "[App] Face delete requested:" << name << "(ID=" << featureId << ")";
    if (m_faceCommandServer) {
        m_faceCommandServer->faceDelete(featureId);
    }
}