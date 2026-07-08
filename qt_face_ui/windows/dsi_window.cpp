#include "dsi_window.h"
#include <QDebug>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>

DSIMainWindow::DSIMainWindow(QWidget *parent)
    : QWidget(parent)
    , m_stackedWidget(nullptr)
    , m_homePage(nullptr)
    , m_displayControlPage(nullptr)
    , m_usbCameraPage(nullptr)
    , m_streamSelector(nullptr)
    , m_showBoxesCheck(nullptr)
    , m_showRoisCheck(nullptr)
    , m_faceListPage(nullptr)
    , m_attendancePage(nullptr)
    , m_deviceUsagePage(nullptr)
    , m_videoRecordPage(nullptr)
    , m_statisticsPage(nullptr)
    , m_systemDashboardPage(nullptr)           // ★ 新增：初始化仪表盘
    , m_deviceManagePage(nullptr)              // ★ 新增：设备管理页面
    , m_usbCameraView(nullptr)
{
    // 设置窗口属性
    setWindowTitle("实验室设备与人员管理系统");
    setWindowFlags(Qt::FramelessWindowHint);
    setStyleSheet("background-color: #2c3e50;");
    
    // ============================================================
    // 创建堆栈窗口管理器（用于切换不同页面）
    // ============================================================
    m_stackedWidget = new QStackedWidget(this);
    
    // ============================================================
    // 创建所有页面
    // ============================================================
    m_homePage = createHomePage();                  // 主页
    m_displayControlPage = createDisplayControlPage();  // 显示控制页面
    m_usbCameraPage = createUsbCameraPage();        // USB 摄像头页面
    m_faceListPage = new FaceListWindow(this);      // 人脸库页面
    m_attendancePage = new AttendanceWindow(this);  // 考勤记录页面
    m_deviceUsagePage = new DeviceUsageWindow(this); // 设备使用页面
    m_videoRecordPage = new VideoRecordWindow(this); // 录像记录页面
    m_statisticsPage = new StatisticsWindow(this);   // 统计图表页面
    m_systemDashboardPage = new SystemDashboard(this); // ★ 新增：系统仪表盘页面
    m_deviceManagePage = new DeviceManageWindow(this); // ★ 新增：设备管理（插座）页面
    
    // ============================================================
    // 将页面添加到堆栈管理器
    // ============================================================
    m_stackedWidget->addWidget(m_homePage);          // 索引 0
    m_stackedWidget->addWidget(m_displayControlPage); // 索引 1
    m_stackedWidget->addWidget(m_usbCameraPage);     // 索引 2
    m_stackedWidget->addWidget(m_faceListPage);      // 索引 3
    m_stackedWidget->addWidget(m_attendancePage);    // 索引 4
    m_stackedWidget->addWidget(m_deviceUsagePage);   // 索引 5
    m_stackedWidget->addWidget(m_videoRecordPage);   // 索引 6
    m_stackedWidget->addWidget(m_statisticsPage);    // 索引 7
    m_stackedWidget->addWidget(m_systemDashboardPage); // ★ 新增：索引 8
    m_stackedWidget->addWidget(m_deviceManagePage);    // ★ 新增：索引 9
    
    // ============================================================
    // 连接各个页面的返回信号
    // 当用户点击页面内的"返回"按钮时，切换回主页
    // ============================================================
    connect(m_faceListPage, &FaceListWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    connect(m_attendancePage, &AttendanceWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    connect(m_deviceUsagePage, &DeviceUsageWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    connect(m_videoRecordPage, &VideoRecordWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    connect(m_statisticsPage, &StatisticsWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    // ★ 新增：仪表盘返回信号
    connect(m_systemDashboardPage, &SystemDashboard::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });

    // ★ 新增：设备管理返回信号
    connect(m_deviceManagePage, &DeviceManageWindow::backToHome, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    // ============================================================
    // 主布局：堆栈窗口铺满整个屏幕
    // ============================================================
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_stackedWidget);
    setLayout(mainLayout);
    
    // 默认显示主页
    m_stackedWidget->setCurrentWidget(m_homePage);
    
    qDebug() << "[DSIWindow] Created";
}

DSIMainWindow::~DSIMainWindow()
{
    qDebug() << "[DSIWindow] Destroyed";
}

// USB 主动登记：识别成功后开插座+倒计时（转发给 DeviceManageWindow）
// 无插排时底层 onDeviceRegister 返回 false，流程继续不阻塞
bool DSIMainWindow::requestRegister(int room_id, int device_id, int duration_minutes)
{
    if (!m_deviceManagePage) return false;
    return m_deviceManagePage->requestRegister(room_id, device_id, duration_minutes);
}

// ================================================================
// 创建 USB 摄像头页面
// 用于人脸识别时显示 USB 摄像头的实时画面
// ================================================================
QWidget* DSIMainWindow::createUsbCameraPage()
{
    QWidget* page = new QWidget();
    
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // 创建 OpenGL 视频显示控件
    // stream_id = -1 表示这是 USB 摄像头，不是网络摄像头
    m_usbCameraView = new GLStreamView(-1, page);
    m_usbCameraView->setShowBoxes(false);
    m_usbCameraView->setStyleSheet("background-color: black;");
    layout->addWidget(m_usbCameraView);
    
    return page;
}

// ================================================================
// 显示 USB 摄像头（人脸识别时调用）
// ================================================================
void DSIMainWindow::showUsbCamera()
{
    if (m_usbCameraPage) {
        if (m_usbCameraView) {
            m_usbCameraView->clearFrame();
            m_usbCameraView->update();
        }
        m_stackedWidget->setCurrentWidget(m_usbCameraPage);
        qDebug() << "[DSIWindow] Showing USB camera";
    }
}

// ================================================================
// 隐藏 USB 摄像头，返回主页
// ================================================================
void DSIMainWindow::hideUsbCamera()
{
    m_stackedWidget->setCurrentWidget(m_homePage);
    qDebug() << "[DSIWindow] Hiding USB camera";
}

// ================================================================
// 更新 USB 摄像头画面帧
// ================================================================
void DSIMainWindow::updateUsbFrame(int fd, int width, int height, int size, int format)
{
    if (m_usbCameraView && m_stackedWidget->currentWidget() == m_usbCameraPage) {
        m_usbCameraView->updateFrame(fd, width, height, size, format);
    } else if (fd >= 0) {
        ::close(fd);
    }
}

// ================================================================
// 创建主页（12个功能按钮）
// ================================================================
QWidget* DSIMainWindow::createHomePage()
{
    QWidget* page = new QWidget();
    
    // ============================================================
    // 创建所有按钮（12个按钮，3行x4列）
    // ============================================================
    QPushButton* btnSignIn = new QPushButton("签到");
    QPushButton* btnSignOut = new QPushButton("签退");
    QPushButton* btnDeviceReg = new QPushButton("设备登记");
    QPushButton* btnFaceEnroll = new QPushButton("人脸录入");
    
    QPushButton* btnQueryAttendance = new QPushButton("考勤记录");
    QPushButton* btnQueryDevice = new QPushButton("设备使用");
    QPushButton* btnQueryVideo = new QPushButton("录像记录");
    QPushButton* btnQueryFace = new QPushButton("人脸库");
    
    QPushButton* btnDisplayControl = new QPushButton("显示控制");
    QPushButton* btnRoi = new QPushButton("ROI配置");
    QPushButton* btnStatistics = new QPushButton("数据统计");
    QPushButton* btnDashboard = new QPushButton("系统状态");

    QPushButton* btnDeviceManage = new QPushButton("设备管理");   // ★ 新增：插座控制

    // ============================================================
    // 设置按钮大小（统一尺寸）
    // ============================================================
    QSize btnSize(140, 80);
    QList<QPushButton*> buttons = {
        btnSignIn, btnSignOut, btnDeviceReg, btnFaceEnroll,
        btnQueryAttendance, btnQueryDevice, btnQueryVideo, btnQueryFace,
        btnDisplayControl, btnRoi, btnStatistics, btnDashboard,
        btnDeviceManage
    };
    for (auto* btn : buttons) {
        btn->setFixedSize(btnSize);
    }
    
    // ============================================================
    // 设置按钮样式
    // ============================================================
    QString btnStyle = "QPushButton {"
                       "    background-color: #34495e;"
                       "    color: white;"
                       "    font-size: 16px;"
                       "    font-weight: bold;"
                       "    border-radius: 10px;"
                       "}"
                       "QPushButton:pressed {"
                       "    background-color: #1a252f;"
                       "}";
    for (auto* btn : buttons) {
        btn->setStyleSheet(btnStyle);
    }
    
    // ============================================================
    // 连接按钮信号到槽函数
    // ============================================================
    connect(btnSignIn, &QPushButton::clicked, this, &DSIMainWindow::onSignInClicked);
    connect(btnSignOut, &QPushButton::clicked, this, &DSIMainWindow::onSignOutClicked);
    connect(btnDeviceReg, &QPushButton::clicked, this, &DSIMainWindow::onDeviceRegisterClicked);
    connect(btnFaceEnroll, &QPushButton::clicked, this, &DSIMainWindow::onFaceEnrollClicked);
    
    connect(btnQueryAttendance, &QPushButton::clicked, this, &DSIMainWindow::onQueryAttendanceClicked);
    connect(btnQueryDevice, &QPushButton::clicked, this, &DSIMainWindow::onQueryDeviceUsageClicked);
    connect(btnQueryVideo, &QPushButton::clicked, this, &DSIMainWindow::onQueryVideoRecordsClicked);
    connect(btnQueryFace, &QPushButton::clicked, this, &DSIMainWindow::onQueryFaceListClicked);
    
    connect(btnDisplayControl, &QPushButton::clicked, [this]() {
        if (m_displayControlPage) {
            m_stackedWidget->setCurrentWidget(m_displayControlPage);
        }
    });
    connect(btnStatistics, &QPushButton::clicked, this, &DSIMainWindow::onStatisticsClicked);
    connect(btnRoi, &QPushButton::clicked, this, &DSIMainWindow::onRoiConfigClicked);
    connect(btnDashboard, &QPushButton::clicked, this, &DSIMainWindow::onDashboardClicked);
    connect(btnDeviceManage, &QPushButton::clicked, this, &DSIMainWindow::onDeviceManageClicked);

    // ============================================================
    // 布局：4行4列的网格布局
    // ============================================================
    QGridLayout* gridLayout = new QGridLayout(page);
    gridLayout->setSpacing(15);
    gridLayout->setContentsMargins(20, 20, 20, 20);

    gridLayout->addWidget(btnSignIn, 0, 0);
    gridLayout->addWidget(btnSignOut, 0, 1);
    gridLayout->addWidget(btnDeviceReg, 0, 2);
    gridLayout->addWidget(btnFaceEnroll, 0, 3);

    gridLayout->addWidget(btnQueryAttendance, 1, 0);
    gridLayout->addWidget(btnQueryDevice, 1, 1);
    gridLayout->addWidget(btnQueryVideo, 1, 2);
    gridLayout->addWidget(btnQueryFace, 1, 3);

    gridLayout->addWidget(btnDisplayControl, 2, 0);
    gridLayout->addWidget(btnRoi, 2, 1);
    gridLayout->addWidget(btnStatistics, 2, 2);
    gridLayout->addWidget(btnDashboard, 2, 3);

    gridLayout->addWidget(btnDeviceManage, 3, 0);

    // 第五行：标题标签（跨4列居中显示）
    QLabel* titleLabel = new QLabel("实验室设备与人员管理系统");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 28px; font-weight: bold; color: #3498db; margin: 20px;");
    gridLayout->addWidget(titleLabel, 4, 0, 1, 4);

    return page;
}

// ================================================================
// 创建显示控制页面
// ================================================================
QWidget* DSIMainWindow::createDisplayControlPage()
{
    QWidget* page = new QWidget();
    
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);
    layout->setContentsMargins(30, 30, 30, 30);
    
    QLabel* title = new QLabel("显示控制");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 24px; font-weight: bold; color: #3498db;");
    layout->addWidget(title);
    
    // ============================================================
    // 单路全屏控制区域
    // ============================================================
    QHBoxLayout* fullscreenLayout = new QHBoxLayout();
    
    QLabel* fullscreenLabel = new QLabel("单路全屏:");
    fullscreenLabel->setStyleSheet("font-size: 18px;");
    fullscreenLabel->setFixedWidth(120);
    
    m_streamSelector = new QComboBox();
    for (int i = 0; i < 8; i++) {
        m_streamSelector->addItem(QString("摄像头 %1").arg(i + 1));
    }
    m_streamSelector->setStyleSheet("font-size: 16px; padding: 8px;");
    m_streamSelector->setFixedHeight(50);
    
    QPushButton* btnApplyFullscreen = new QPushButton("全屏显示");
    btnApplyFullscreen->setFixedSize(120, 50);
    btnApplyFullscreen->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #229954; }"
    );
    
    fullscreenLayout->addWidget(fullscreenLabel);
    fullscreenLayout->addWidget(m_streamSelector);
    fullscreenLayout->addWidget(btnApplyFullscreen);
    fullscreenLayout->addStretch();
    layout->addLayout(fullscreenLayout);
    
    // ============================================================
    // 多路网格控制区域
    // ============================================================
    QHBoxLayout* gridLayoutH = new QHBoxLayout();
    
    QLabel* gridLabel = new QLabel("多路网格:");
    gridLabel->setStyleSheet("font-size: 18px;");
    gridLabel->setFixedWidth(120);
    
    QPushButton* btnGrid = new QPushButton("2x4 网格显示");
    btnGrid->setFixedSize(200, 50);
    btnGrid->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-size: 16px; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #1f618d; }"
    );
    
    gridLayoutH->addWidget(gridLabel);
    gridLayoutH->addWidget(btnGrid);
    gridLayoutH->addStretch();
    layout->addLayout(gridLayoutH);
    
    // ============================================================
    // 检测框显示开关区域
    // ============================================================
    QHBoxLayout* boxesLayout = new QHBoxLayout();

    QLabel* boxesLabel = new QLabel("检测框显示:");
    boxesLabel->setStyleSheet("font-size: 18px;");
    boxesLabel->setFixedWidth(120);

    m_showBoxesCheck = new QCheckBox("显示人形检测框");
    m_showBoxesCheck->setChecked(true);
    m_showBoxesCheck->setStyleSheet("font-size: 16px;");
    m_showBoxesCheck->setFixedHeight(50);

    boxesLayout->addWidget(boxesLabel);
    boxesLayout->addWidget(m_showBoxesCheck);
    boxesLayout->addStretch();
    layout->addLayout(boxesLayout);

    // ============================================================
    // ROI 区域显示开关区域
    // ============================================================
    QHBoxLayout* roisLayout = new QHBoxLayout();

    QLabel* roisLabel = new QLabel("ROI 区域:");
    roisLabel->setStyleSheet("font-size: 18px;");
    roisLabel->setFixedWidth(120);

    m_showRoisCheck = new QCheckBox("显示设备 ROI 区域");
    m_showRoisCheck->setChecked(false);
    m_showRoisCheck->setStyleSheet("font-size: 16px;");
    m_showRoisCheck->setFixedHeight(50);

    roisLayout->addWidget(roisLabel);
    roisLayout->addWidget(m_showRoisCheck);
    roisLayout->addStretch();
    layout->addLayout(roisLayout);
    
    // ============================================================
    // 返回主页按钮
    // ============================================================
    QPushButton* btnBack = new QPushButton("返回主页");
    btnBack->setFixedSize(200, 60);
    btnBack->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-size: 18px; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #6c7a7a; }"
    );
    
    layout->addStretch();
    layout->addWidget(btnBack, 0, Qt::AlignCenter);
    
    // ============================================================
    // 连接信号
    // ============================================================
    connect(btnApplyFullscreen, &QPushButton::clicked, [this]() {
        int stream_id = m_streamSelector->currentIndex();
        qDebug() << "[DSI] Fullscreen selected stream:" << stream_id;
        emit switchToFullscreen(stream_id);
    });
    
    connect(btnGrid, &QPushButton::clicked, [this]() {
        qDebug() << "[DSI] Grid mode clicked";
        emit switchToGrid();
    });
    
    connect(m_showBoxesCheck, &QCheckBox::toggled, [this](bool checked) {
        qDebug() << "[DSI] Show boxes toggled:" << checked;
        emit toggleShowBoxes(checked);
    });

    connect(m_showRoisCheck, &QCheckBox::toggled, [this](bool checked) {
        qDebug() << "[DSI] Show ROIs toggled:" << checked;
        emit toggleShowRois(checked);
    });
    
    connect(btnBack, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentWidget(m_homePage);
    });
    
    return page;
}

// ================================================================
// 槽函数实现
// ================================================================

void DSIMainWindow::onSignInClicked()
{
    qDebug() << "[DSI] Sign In clicked";
    emit signInRequested();
}

void DSIMainWindow::onSignOutClicked()
{
    qDebug() << "[DSI] Sign Out clicked";
    emit signOutRequested();
}

void DSIMainWindow::onDeviceRegisterClicked()
{
    qDebug() << "[DSI] Device Register clicked";
    emit deviceRegisterRequested();
}

void DSIMainWindow::onFaceEnrollClicked()
{
    qDebug() << "[DSI] Face Enroll clicked";
    emit faceEnrollRequested();
}

void DSIMainWindow::onQueryAttendanceClicked()
{
    qDebug() << "[DSI] Query Attendance clicked";
    if (m_attendancePage) {
        m_stackedWidget->setCurrentWidget(m_attendancePage);
    }
}

void DSIMainWindow::onQueryDeviceUsageClicked()
{
    qDebug() << "[DSI] Query Device Usage clicked";
    if (m_deviceUsagePage) {
        m_stackedWidget->setCurrentWidget(m_deviceUsagePage);
    }
}

void DSIMainWindow::onQueryVideoRecordsClicked()
{
    qDebug() << "[DSI] Query Video Records clicked";
    if (m_videoRecordPage) {
        m_stackedWidget->setCurrentWidget(m_videoRecordPage);
    }
}

void DSIMainWindow::onQueryFaceListClicked()
{
    qDebug() << "[DSI] Query Face List clicked";
    if (m_faceListPage) {
        connect(m_faceListPage, &FaceListWindow::faceDeleteRequested,
                this, &DSIMainWindow::faceDeleteRequested);
        m_stackedWidget->setCurrentWidget(m_faceListPage);
    }
}

void DSIMainWindow::onStatisticsClicked()
{
    qDebug() << "[DSI] Statistics clicked";
    if (m_statisticsPage) {
        m_stackedWidget->setCurrentWidget(m_statisticsPage);
    }
}

void DSIMainWindow::onRoiConfigClicked()
{
    qDebug() << "[DSI] ROI Config clicked";
    emit roiConfigRequested();
}

// ★ 修改：系统状态按钮跳转到仪表盘页面
void DSIMainWindow::onDashboardClicked()
{
    qDebug() << "[DSI] Dashboard clicked";
    if (m_systemDashboardPage) {
        m_stackedWidget->setCurrentWidget(m_systemDashboardPage);
    }
}

// ★ 新增：设备管理按钮跳转到设备管理页面
void DSIMainWindow::onDeviceManageClicked()
{
    qDebug() << "[DSI] Device Manage clicked";
    if (m_deviceManagePage) {
        m_stackedWidget->setCurrentWidget(m_deviceManagePage);
    }
}