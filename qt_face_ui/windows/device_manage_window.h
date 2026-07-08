#ifndef DEVICE_MANAGE_WINDOW_H
#define DEVICE_MANAGE_WINDOW_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QFrame>
#include <QFormLayout>
#include <QTimer>
#include <QTabWidget>
#include "ipc/device_ipc_client.h"

/**
 * @brief 设备管理页面（插座 + ESP32 管理）
 *
 * 使用 QTabWidget 分两个 Tab：
 *   - Tab1 "智能插座"：房间选择 + 插座状态表 + 新增插座
 *   - Tab2 "ESP32摄像头"：ESP32 列表 + 新增/删除 ESP32
 *
 * 数据来源：
 * - 实时状态：直接通过 IPC 查询 device_process（不经主进程）
 * - 手动断电：通过 IPC 发给主进程，主进程清状态后转发 RELEASE 给 device_process
 *
 * 自动刷新：可见时每 2 秒拉取一次状态
 */
class DeviceManageWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceManageWindow(QWidget *parent = nullptr);
    ~DeviceManageWindow();

    // USB 主动登记：识别成功后开插座+倒计时（无插排返回false，流程继续）
    bool requestRegister(int room_id, int device_id, int duration_minutes);

signals:
    void backToHome();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    // 插座 Tab
    void onRefreshRooms();
    void onRoomChanged(int index);
    void onRefreshStatus();
    void onAddPlugClicked();
    void onConfirmAddPlug();
    void onCancelAddPlug();
    void onPowerOffClicked(int row);
    void onDeletePlugClicked(int room_id, int device_id, const QString& name);

    // ESP32 Tab
    void onRefreshEsp32();
    void onAddEsp32Clicked();
    void onConfirmAddEsp32();
    void onCancelAddEsp32();
    void onDeleteEsp32Clicked(int room_id, const QString& name);

    void onBack();
    void onAutoRefresh();

private:
    void setupUI();
    void setupPlugTab(QWidget* tab);
    void setupEsp32Tab(QWidget* tab);
    void refreshStatusAsync();
    void showAddPlugPanel(bool show);   // 显示/隐藏新增插座面板
    void showAddEsp32Panel(bool show); // 显示/隐藏新增 ESP32 面板

    // UI - 通用
    QTabWidget*     m_tabWidget;
    QPushButton*    m_backBtn;

    // UI - 插座 Tab
    QComboBox*      m_roomCombo;
    QTableWidget*   m_table;
    QPushButton*    m_refreshBtn;
    QPushButton*    m_addPlugBtn;

    // UI - 新增插座内嵌面板
    QFrame*         m_addPlugPanel;
    QComboBox*      m_addRoomCombo;
    QComboBox*      m_addDeviceCombo;
    QLineEdit*      m_nameEdit;
    QLineEdit*      m_ipEdit;
    QLineEdit*      m_tokenEdit;
    QPushButton*    m_confirmAddBtn;
    QPushButton*    m_cancelAddBtn;

    // UI - ESP32 Tab
    QTableWidget*   m_esp32Table;
    QPushButton*    m_esp32RefreshBtn;
    QPushButton*    m_addEsp32Btn;

    // UI - 新增 ESP32 内嵌面板
    QFrame*         m_addEsp32Panel;
    QComboBox*      m_esp32RoomCombo;
    QLineEdit*      m_esp32NameEdit;
    QLineEdit*      m_esp32IpEdit;
    QLineEdit*      m_esp32RtspEdit;
    QPushButton*    m_esp32ConfirmAddBtn;
    QPushButton*    m_esp32CancelAddBtn;

    // 业务
    DeviceIpcClient m_ipc;
    QTimer*         m_timer;
    int             m_currentRoom;  // 当前选中的房间号（插座 Tab）
};

#endif // DEVICE_MANAGE_WINDOW_H
