#ifndef DSI_WINDOW_H
#define DSI_WINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>

#include "windows/face_list_window.h"
#include "windows/attendance_window.h"
#include "windows/device_usage_window.h"
#include "windows/video_record_window.h"
#include "windows/statistics_window.h"
#include "windows/system_dashboard.h"      // ★ 新增：系统仪表盘
#include "widgets/gl_stream_view.h"

/**
 * @class DSIMainWindow
 * @brief DSI 触摸屏主窗口
 * 
 * 功能概述：
 * 1. 使用 QStackedWidget 管理多个页面（主页、显示控制、人脸库、考勤记录等）
 * 2. 提供12个功能按钮：签到、签退、设备登记、人脸录入、考勤记录、
 *    设备使用、录像记录、人脸库、显示控制、ROI配置、数据统计、系统状态
 * 3. 人脸识别时切换到 USB 摄像头页面显示实时画面
 * 4. 通过信号将用户操作转发给 FaceApplication
 */
class DSIMainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DSIMainWindow(QWidget *parent = nullptr);
    ~DSIMainWindow();

signals:
    // ============================================================
    // 人脸识别操作信号（发送给 FaceApplication）
    // ============================================================
    void signInRequested();           // 签到
    void signOutRequested();          // 签退
    void deviceRegisterRequested();   // 设备登记
    void faceEnrollRequested();       // 人脸录入
    
    // ============================================================
    // HDMI 大屏显示控制信号
    // ============================================================
    void switchToFullscreen(int stream_id);   // 切换到指定摄像头的全屏模式
    void switchToGrid();                      // 切换到 2x4 网格模式
    void toggleShowBoxes(bool show);          // 开关人形检测框显示
    void toggleShowRois(bool show);           // 开关 ROI 区域显示
    
    // ============================================================
    // 配置信号
    // ============================================================
    void roiConfigRequested();          // ROI 区域配置请求
    void systemDashboardRequested();    // 系统状态仪表盘请求
    
    // ============================================================
    // 人脸删除信号（从人脸库页面转发）
    // ============================================================
    void faceDeleteRequested(int featureId, const QString& name);

public slots:
    // ============================================================
    // USB 摄像头控制（人脸识别时调用）
    // ============================================================
    void showUsbCamera();               // 显示 USB 摄像头画面
    void hideUsbCamera();               // 隐藏 USB 摄像头画面，返回主页
    void updateUsbFrame(int fd, int width, int height, int size, int format);  // 更新画面帧

private slots:
    // ============================================================
    // 主页按钮点击处理
    // ============================================================
    void onSignInClicked();             // 签到
    void onSignOutClicked();            // 签退
    void onDeviceRegisterClicked();     // 设备登记
    void onFaceEnrollClicked();         // 人脸录入
    
    void onQueryAttendanceClicked();    // 考勤记录
    void onQueryDeviceUsageClicked();   // 设备使用
    void onQueryVideoRecordsClicked();  // 录像记录
    void onQueryFaceListClicked();      // 人脸库
    
    void onStatisticsClicked();         // 数据统计
    void onRoiConfigClicked();          // ROI 配置
    void onDashboardClicked();          // 系统状态（★ 修改：跳转到仪表盘页面）

private:
    // ============================================================
    // 页面创建函数
    // ============================================================
    QWidget* createHomePage();              // 创建主页（12个功能按钮）
    QWidget* createDisplayControlPage();    // 创建显示控制页面
    QWidget* createUsbCameraPage();         // 创建 USB 摄像头页面

    // ============================================================
    // 成员变量
    // ============================================================
    QStackedWidget* m_stackedWidget;        // 堆栈窗口管理器，管理所有页面
    
    // 页面指针
    QWidget* m_homePage;                    // 主页
    QWidget* m_displayControlPage;          // 显示控制页面
    QWidget* m_usbCameraPage;               // USB 摄像头页面
    
    // 显示控制页面控件
    QComboBox* m_streamSelector;            // 摄像头选择下拉框（0-7）
    QCheckBox* m_showBoxesCheck;            // 检测框显示开关
    QCheckBox* m_showRoisCheck;             // ROI 区域显示开关
    
    // 各个功能页面（独立窗口类）
    FaceListWindow* m_faceListPage;         // 人脸库页面
    AttendanceWindow* m_attendancePage;     // 考勤记录页面
    DeviceUsageWindow* m_deviceUsagePage;   // 设备使用页面
    VideoRecordWindow* m_videoRecordPage;   // 录像记录页面
    StatisticsWindow* m_statisticsPage;     // 数据统计页面
    SystemDashboard*  m_systemDashboardPage; // ★ 新增：系统仪表盘页面
    
    // USB 摄像头视频显示控件
    GLStreamView* m_usbCameraView;          // OpenGL 视频显示控件
};

#endif