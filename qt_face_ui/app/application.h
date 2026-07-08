#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>
#include <QWidget>
#include <QTimer>
#include "app/constants.h"

class HDMIMainWindow;
class DSIMainWindow;
class FrameReceiver;
class FaceIpcServer;
class FaceListWindow;
class RoiConfigWindow;

class FaceApplication : public QApplication
{
    Q_OBJECT

public:
    FaceApplication(int &argc, char **argv);
    ~FaceApplication();

    bool initialize();

    HDMIMainWindow* getHdmiWindow() const { return m_hdmiWindow; }
    DSIMainWindow* getDsiWindow() const { return m_dsiWindow; }

private slots:
    // 帧接收回调（带 format 参数）
    void onMainFrameReady(int stream_id, int fd, int width, int height, int size, int format);
    void onMainBoxesReady(int stream_id, QVector<DetectionBox> boxes);
    void onMainDeviceStatusReady(int stream_id, QVector<int> deviceIds, QVector<bool> occupied);
    void onUsbFrameReady(int stream_id, int fd, int width, int height, int size, int format);
    
    void onCommandResult(int result, const QString& replyName);
    void onFaceClientConnected();
    
    void onSwitchToFullscreen(int stream_id);
    void onSwitchToGrid();
    void onToggleShowBoxes(bool show);
    void onToggleShowRois(bool show);
    void onRoiConfigRequested();
    void onRoiSaved();
    void onRoiBackToHome();
    void onSignInRequested();
    void onSignOutRequested();
    void onDeviceRegisterRequested();
    void onFaceEnrollRequested();
    void onFaceDeleteRequested(int featureId, const QString& name);

private:
    void setupWindows();
    void setupFrameReceivers();
    void setupCommandServer();
    void connectDsiSignals();
    void connectFaceListSignals();
    void setupRoiConfigPage();

    HDMIMainWindow*     m_hdmiWindow;
    DSIMainWindow*      m_dsiWindow;
    FrameReceiver*      m_mainFrameReceiver;
    FrameReceiver*      m_usbFrameReceiver;
    FaceIpcServer*      m_faceCommandServer;
    FaceListWindow*     m_faceListPage;
    RoiConfigWindow*    m_roiConfigPage;

    bool                m_showingUsb;
    QTimer*             m_usbHideTimer;

    // USB 主动登记的待处理信息（仅在 onDeviceRegisterRequested 后有效）
    // m_pendingRegMode: 0=无, 2=设备登记（与 face_process case 编号一致）
    int                 m_pendingRegMode;
    int                 m_pendingRegRoom;
    int                 m_pendingRegDevice;
    int                 m_pendingRegDuration;
};

#endif