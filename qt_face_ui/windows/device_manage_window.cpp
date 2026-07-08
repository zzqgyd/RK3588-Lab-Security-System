#include "device_manage_window.h"
#include <QHeaderView>
#include <QDebug>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QApplication>

// ================================================================
// 构造 / 析构
// ================================================================
DeviceManageWindow::DeviceManageWindow(QWidget *parent)
    : QWidget(parent)
    , m_tabWidget(nullptr)
    , m_backBtn(nullptr)
    , m_roomCombo(nullptr)
    , m_table(nullptr)
    , m_refreshBtn(nullptr)
    , m_addPlugBtn(nullptr)
    , m_addPlugPanel(nullptr)
    , m_addRoomCombo(nullptr)
    , m_addDeviceCombo(nullptr)
    , m_nameEdit(nullptr)
    , m_ipEdit(nullptr)
    , m_tokenEdit(nullptr)
    , m_confirmAddBtn(nullptr)
    , m_cancelAddBtn(nullptr)
    , m_esp32Table(nullptr)
    , m_esp32RefreshBtn(nullptr)
    , m_addEsp32Btn(nullptr)
    , m_addEsp32Panel(nullptr)
    , m_esp32RoomCombo(nullptr)
    , m_esp32NameEdit(nullptr)
    , m_esp32IpEdit(nullptr)
    , m_esp32RtspEdit(nullptr)
    , m_esp32ConfirmAddBtn(nullptr)
    , m_esp32CancelAddBtn(nullptr)
    , m_timer(nullptr)
    , m_currentRoom(0)
{
    setupUI();

    // 启动 IPC 服务端（监听 SOCK_PATH_QT_DEVICE，等待 device_process 连接）
    m_ipc.startServer();

    // 自动刷新定时器（2 秒）
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &DeviceManageWindow::onAutoRefresh);

    // 初始加载一次
    onRefreshRooms();
    onRefreshEsp32();
}

DeviceManageWindow::~DeviceManageWindow()
{
    // 1. 先停自动刷新定时器（避免定时器还在触发 sendCmdRecvResp）
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    // 2. 再停 IPC 服务端（此时不会有新的查询请求）
    m_ipc.stopServer();
}

// USB 主动登记：识别成功后开插座+倒计时
bool DeviceManageWindow::requestRegister(int room_id, int device_id, int duration_minutes)
{
    return m_ipc.requestRegister(room_id, device_id, duration_minutes);
}

// ================================================================
// UI 搭建
// ================================================================
void DeviceManageWindow::setupUI()
{
    setStyleSheet("background-color: #2c3e50;");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // 标题
    QLabel* title = new QLabel("设备管理");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 24px; font-weight: bold; color: #3498db;");
    mainLayout->addWidget(title);

    // Tab 容器
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #34495e; border-radius: 4px; }"
        "QTabBar::tab { background-color: #34495e; color: white; padding: 8px 20px; "
        "  font-size: 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background-color: #3498db; }");
    mainLayout->addWidget(m_tabWidget);

    // Tab 1: 智能插座
    QWidget* plugTab = new QWidget();
    setupPlugTab(plugTab);
    m_tabWidget->addTab(plugTab, "智能插座");

    // Tab 2: ESP32 摄像头
    QWidget* esp32Tab = new QWidget();
    setupEsp32Tab(esp32Tab);
    m_tabWidget->addTab(esp32Tab, "ESP32摄像头");

    // 底部返回按钮
    QHBoxLayout* bottomBar = new QHBoxLayout();
    bottomBar->addStretch();
    m_backBtn = new QPushButton("返回");
    m_backBtn->setFixedSize(120, 50);
    m_backBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-size: 16px; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #6c7a7a; }");
    bottomBar->addWidget(m_backBtn);
    bottomBar->addStretch();
    mainLayout->addLayout(bottomBar);

    connect(m_backBtn, &QPushButton::clicked, this, &DeviceManageWindow::onBack);
}

// ================================================================
// 插座 Tab
// ================================================================
void DeviceManageWindow::setupPlugTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 顶部控制栏
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setSpacing(10);

    QLabel* roomLabel = new QLabel("房间:");
    roomLabel->setStyleSheet("font-size: 16px; color: white;");
    m_roomCombo = new QComboBox();
    m_roomCombo->setStyleSheet("font-size: 16px; padding: 6px;");
    m_roomCombo->setFixedHeight(40);
    for (int i = 0; i < 8; ++i) {
        m_roomCombo->addItem(QString("房间 %1").arg(i), i);
    }

    m_refreshBtn = new QPushButton("刷新");
    m_addPlugBtn = new QPushButton("新增插座");
    QString btnStyle = "QPushButton { background-color: #27ae60; color: white; "
                       "  font-size: 16px; border-radius: 8px; padding: 8px 16px; }"
                       "QPushButton:pressed { background-color: #229954; }";
    m_refreshBtn->setStyleSheet(btnStyle);
    m_addPlugBtn->setStyleSheet("QPushButton { background-color: #2980b9; color: white; "
                                "  font-size: 16px; border-radius: 8px; padding: 8px 16px; }"
                                "QPushButton:pressed { background-color: #1f618d; }");

    topBar->addWidget(roomLabel);
    topBar->addWidget(m_roomCombo);
    topBar->addStretch();
    topBar->addWidget(m_refreshBtn);
    topBar->addWidget(m_addPlugBtn);
    layout->addLayout(topBar);

    // 表格
    m_table = new QTableWidget(tab);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setStyleSheet(
        "QTableWidget { background-color: white; alternate-background-color: #f5f5f5; }"
        "QHeaderView::section { background-color: #3498db; color: white; padding: 5px; }"
    );
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels(
        QStringList() << "设备号" << "名称" << "在线" << "开关" << "功率(W)"
                      << "电量(KWh)" << "温度(℃)" << "倒计时(分)" << "故障" << "操作");
    layout->addWidget(m_table);

    connect(m_roomCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onRoomChanged(int)));
    connect(m_refreshBtn, &QPushButton::clicked, this, &DeviceManageWindow::onRefreshStatus);
    connect(m_addPlugBtn, &QPushButton::clicked, this, &DeviceManageWindow::onAddPlugClicked);

    // 新增插座内嵌面板（覆盖在表格区域上方）
    m_addPlugPanel = new QFrame(tab);
    m_addPlugPanel->setStyleSheet(
        "QFrame { background-color: #34495e; border: 2px solid #3498db; border-radius: 10px; }"
        "QLabel { color: white; font-size: 16px; }"
        "QLineEdit { font-size: 16px; padding: 6px; border: 1px solid #555; border-radius: 4px; background-color: white; }"
        "QComboBox { font-size: 16px; padding: 6px; }");
    m_addPlugPanel->hide();

    QFormLayout* addForm = new QFormLayout(m_addPlugPanel);
    addForm->setContentsMargins(30, 30, 30, 30);
    addForm->setSpacing(12);

    QLabel* addTitle = new QLabel("新增插座");
    addTitle->setStyleSheet("font-size: 20px; font-weight: bold; color: #3498db;");
    addForm->addRow(addTitle);

    m_addRoomCombo = new QComboBox();
    for (int i = 0; i < 8; ++i) m_addRoomCombo->addItem(QString::number(i), i);
    m_addDeviceCombo = new QComboBox();
    for (int i = 1; i <= 4; ++i) m_addDeviceCombo->addItem(QString::number(i), i);

    m_nameEdit  = new QLineEdit();
    m_nameEdit->setPlaceholderText("如：示波器");
    m_ipEdit    = new QLineEdit();
    m_ipEdit->setPlaceholderText("如：192.168.1.100");
    m_tokenEdit = new QLineEdit();
    m_tokenEdit->setPlaceholderText("32 位 hex token");

    addForm->addRow("房间号:", m_addRoomCombo);
    addForm->addRow("设备号:", m_addDeviceCombo);
    addForm->addRow("设备名称:", m_nameEdit);
    addForm->addRow("插座 IP:", m_ipEdit);
    addForm->addRow("Token:", m_tokenEdit);

    QHBoxLayout* addBtnBar = new QHBoxLayout();
    m_confirmAddBtn = new QPushButton("确认添加");
    m_cancelAddBtn  = new QPushButton("取消");
    m_confirmAddBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 8px; padding: 8px 20px; }"
        "QPushButton:pressed { background-color: #229954; }");
    m_cancelAddBtn->setStyleSheet(
        "QPushButton { background-color: #e74c3c; color: white; font-size: 16px; border-radius: 8px; padding: 8px 20px; }"
        "QPushButton:pressed { background-color: #c0392b; }");
    addBtnBar->addStretch();
    addBtnBar->addWidget(m_confirmAddBtn);
    addBtnBar->addWidget(m_cancelAddBtn);
    addForm->addRow(addBtnBar);

    connect(m_confirmAddBtn, &QPushButton::clicked, this, &DeviceManageWindow::onConfirmAddPlug);
    connect(m_cancelAddBtn,  &QPushButton::clicked, this, &DeviceManageWindow::onCancelAddPlug);
}

// ================================================================
// ESP32 Tab
// ================================================================
void DeviceManageWindow::setupEsp32Tab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 顶部控制栏
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setSpacing(10);
    topBar->addStretch();

    m_esp32RefreshBtn = new QPushButton("刷新");
    m_addEsp32Btn     = new QPushButton("新增ESP32");
    m_esp32RefreshBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 8px; padding: 8px 16px; }"
        "QPushButton:pressed { background-color: #229954; }");
    m_addEsp32Btn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-size: 16px; border-radius: 8px; padding: 8px 16px; }"
        "QPushButton:pressed { background-color: #1f618d; }");
    topBar->addWidget(m_esp32RefreshBtn);
    topBar->addWidget(m_addEsp32Btn);
    layout->addLayout(topBar);

    // ESP32 表格
    m_esp32Table = new QTableWidget(tab);
    m_esp32Table->setAlternatingRowColors(true);
    m_esp32Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_esp32Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_esp32Table->horizontalHeader()->setStretchLastSection(true);
    m_esp32Table->setStyleSheet(
        "QTableWidget { background-color: white; alternate-background-color: #f5f5f5; }"
        "QHeaderView::section { background-color: #3498db; color: white; padding: 5px; }"
    );
    m_esp32Table->setColumnCount(6);
    m_esp32Table->setHorizontalHeaderLabels(
        QStringList() << "房间号" << "名称" << "ESP32 IP" << "RTSP URL" << "在线" << "操作");
    layout->addWidget(m_esp32Table);

    connect(m_esp32RefreshBtn, &QPushButton::clicked, this, &DeviceManageWindow::onRefreshEsp32);
    connect(m_addEsp32Btn, &QPushButton::clicked, this, &DeviceManageWindow::onAddEsp32Clicked);

    // 新增 ESP32 内嵌面板
    m_addEsp32Panel = new QFrame(tab);
    m_addEsp32Panel->setStyleSheet(
        "QFrame { background-color: #34495e; border: 2px solid #3498db; border-radius: 10px; }"
        "QLabel { color: white; font-size: 16px; }"
        "QLineEdit { font-size: 16px; padding: 6px; border: 1px solid #555; border-radius: 4px; background-color: white; }"
        "QComboBox { font-size: 16px; padding: 6px; }");
    m_addEsp32Panel->hide();

    QFormLayout* addForm = new QFormLayout(m_addEsp32Panel);
    addForm->setContentsMargins(30, 30, 30, 30);
    addForm->setSpacing(12);

    QLabel* addTitle = new QLabel("新增 ESP32");
    addTitle->setStyleSheet("font-size: 20px; font-weight: bold; color: #3498db;");
    addForm->addRow(addTitle);

    m_esp32RoomCombo = new QComboBox();
    for (int i = 0; i < 8; ++i) m_esp32RoomCombo->addItem(QString("房间 %1").arg(i), i);

    m_esp32NameEdit = new QLineEdit();
    m_esp32NameEdit->setPlaceholderText("如：实验室ESP32");
    m_esp32IpEdit   = new QLineEdit();
    m_esp32IpEdit->setPlaceholderText("如：192.168.1.50（可留空）");
    m_esp32RtspEdit = new QLineEdit();
    m_esp32RtspEdit->setPlaceholderText("如：rtsp://192.168.1.50:8554/stream 或 /home/video.mp4");

    addForm->addRow("房间号:", m_esp32RoomCombo);
    addForm->addRow("名称:", m_esp32NameEdit);
    addForm->addRow("ESP32 IP:", m_esp32IpEdit);
    addForm->addRow("RTSP URL:", m_esp32RtspEdit);

    QHBoxLayout* addBtnBar = new QHBoxLayout();
    m_esp32ConfirmAddBtn = new QPushButton("确认添加");
    m_esp32CancelAddBtn  = new QPushButton("取消");
    m_esp32ConfirmAddBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 8px; padding: 8px 20px; }"
        "QPushButton:pressed { background-color: #229954; }");
    m_esp32CancelAddBtn->setStyleSheet(
        "QPushButton { background-color: #e74c3c; color: white; font-size: 16px; border-radius: 8px; padding: 8px 20px; }"
        "QPushButton:pressed { background-color: #c0392b; }");
    addBtnBar->addStretch();
    addBtnBar->addWidget(m_esp32ConfirmAddBtn);
    addBtnBar->addWidget(m_esp32CancelAddBtn);
    addForm->addRow(addBtnBar);

    connect(m_esp32ConfirmAddBtn, &QPushButton::clicked, this, &DeviceManageWindow::onConfirmAddEsp32);
    connect(m_esp32CancelAddBtn,  &QPushButton::clicked, this, &DeviceManageWindow::onCancelAddEsp32);
}

// ================================================================
// showAddPlugPanel：显示/隐藏新增插座面板
// ----------------------------------------------------------------
// 显示时：面板覆盖在表格上方，暂停自动刷新
// 隐藏时：恢复表格，恢复自动刷新
// ================================================================
void DeviceManageWindow::showAddPlugPanel(bool show)
{
    if (show) {
        // 预填当前房间号
        m_addRoomCombo->setCurrentIndex(m_currentRoom);
        // 清空输入
        m_nameEdit->clear();
        m_ipEdit->clear();
        m_tokenEdit->clear();
        // 面板覆盖表格区域
        m_addPlugPanel->setGeometry(m_table->geometry());
        m_addPlugPanel->raise();
        m_addPlugPanel->show();
        m_nameEdit->setFocus();
        // 暂停自动刷新
        m_timer->stop();
    } else {
        m_addPlugPanel->hide();
        m_timer->start();
    }
}

// ================================================================
// showAddEsp32Panel：显示/隐藏新增 ESP32 面板
// ================================================================
void DeviceManageWindow::showAddEsp32Panel(bool show)
{
    if (show) {
        m_esp32NameEdit->clear();
        m_esp32IpEdit->clear();
        m_esp32RtspEdit->clear();
        m_addEsp32Panel->setGeometry(m_esp32Table->geometry());
        m_addEsp32Panel->raise();
        m_addEsp32Panel->show();
        m_esp32NameEdit->setFocus();
        m_timer->stop();
    } else {
        m_addEsp32Panel->hide();
        m_timer->start();
    }
}

// ================================================================
// 显示/隐藏事件：启动/停止自动刷新
// ================================================================
void DeviceManageWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    onRefreshRooms();
    onRefreshEsp32();
    m_timer->start();
}

void DeviceManageWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

// ================================================================
// 刷新房间列表（从 device_process 拉取有插座的房间）
// ================================================================
void DeviceManageWindow::onRefreshRooms()
{
    QVector<int> rooms;
    if (!m_ipc.listRooms(rooms)) {
        qDebug() << "[DeviceManage] listRooms failed (device_process 未启动?)";
    }

    // 如果没有插座，默认显示房间 0
    if (rooms.isEmpty()) {
        m_roomCombo->clear();
        for (int i = 0; i < 8; ++i) {
            m_roomCombo->addItem(QString("房间 %1").arg(i), i);
        }
        m_currentRoom = 0;
        m_roomCombo->setCurrentIndex(0);
    } else {
        // 保留所有 8 个房间选项（即使没有插座也允许查看），方便新增
        m_roomCombo->blockSignals(true);
        m_roomCombo->clear();
        for (int i = 0; i < 8; ++i) {
            m_roomCombo->addItem(QString("房间 %1").arg(i), i);
        }
        m_roomCombo->blockSignals(false);
        m_currentRoom = rooms[0];
        m_roomCombo->setCurrentIndex(m_currentRoom);
    }
    onRefreshStatus();
}

// ================================================================
// 房间切换
// ================================================================
void DeviceManageWindow::onRoomChanged(int index)
{
    m_currentRoom = m_roomCombo->itemData(index).toInt();
    onRefreshStatus();
}

// ================================================================
// 同步刷新状态（直接调用，可能在主线程阻塞短时间）
// ================================================================
void DeviceManageWindow::onRefreshStatus()
{
    QVector<DevicePlugStatus> statuses;
    if (!m_ipc.queryRoomStatus(m_currentRoom, statuses)) {
        qDebug() << "[DeviceManage] queryRoomStatus failed for room" << m_currentRoom;
        m_table->setRowCount(0);
        return;
    }

    m_table->setRowCount(statuses.size());

    for (int i = 0; i < statuses.size(); ++i) {
        const DevicePlugStatus& s = statuses[i];

        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(s.deviceId)));
        m_table->setItem(i, 1, new QTableWidgetItem(s.name));
        m_table->setItem(i, 2, new QTableWidgetItem(s.online ? "在线" : "离线"));
        m_table->setItem(i, 3, new QTableWidgetItem(s.isOn ? "开" : "关"));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(s.powerW, 'f', 1)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(s.energyKwh, 'f', 3)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(s.temperature)));
        m_table->setItem(i, 7, new QTableWidgetItem(QString::number(s.countdownLeftMin)));
        m_table->setItem(i, 8, new QTableWidgetItem(s.faultDesc));

        // 操作列：断电 + 删除两个按钮
        QWidget* opWidget = new QWidget();
        QHBoxLayout* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(2, 2, 2, 2);
        opLayout->setSpacing(4);

        QPushButton* powerOffBtn = new QPushButton("断电");
        powerOffBtn->setStyleSheet(
            "QPushButton { background-color: #c0392b; color: white; border-radius: 6px; padding: 6px; }"
            "QPushButton:pressed { background-color: #922b21; }");
        powerOffBtn->setProperty("row", i);
        powerOffBtn->setProperty("room_id", s.roomId);
        powerOffBtn->setProperty("device_id", s.deviceId);
        powerOffBtn->setProperty("name", s.name);
        connect(powerOffBtn, &QPushButton::clicked, this, [this, powerOffBtn]() {
            int row = powerOffBtn->property("row").toInt();
            onPowerOffClicked(row);
        });

        QPushButton* deleteBtn = new QPushButton("删除");
        deleteBtn->setStyleSheet(
            "QPushButton { background-color: #7f8c8d; color: white; border-radius: 6px; padding: 6px; }"
            "QPushButton:pressed { background-color: #566573; }");
        deleteBtn->setProperty("room_id", s.roomId);
        deleteBtn->setProperty("device_id", s.deviceId);
        deleteBtn->setProperty("name", s.name);
        connect(deleteBtn, &QPushButton::clicked, this, [this, deleteBtn]() {
            int roomId = deleteBtn->property("room_id").toInt();
            int devId = deleteBtn->property("device_id").toInt();
            QString name = deleteBtn->property("name").toString();
            onDeletePlugClicked(roomId, devId, name);
        });

        opLayout->addWidget(powerOffBtn);
        opLayout->addWidget(deleteBtn);
        m_table->setCellWidget(i, 9, opWidget);
    }

    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 60);
    m_table->setColumnWidth(3, 60);
    m_table->setColumnWidth(4, 80);
    m_table->setColumnWidth(5, 90);
    m_table->setColumnWidth(6, 70);
    m_table->setColumnWidth(7, 80);
    m_table->setColumnWidth(8, 60);
    m_table->setColumnWidth(9, 140);   // 操作列：两个按钮
}

// ================================================================
// 异步刷新状态（定时器触发，避免阻塞 UI）
// ================================================================
void DeviceManageWindow::refreshStatusAsync()
{
    // QtConcurrent 在工作线程执行 IPC，完成后回主线程更新 UI
    int room = m_currentRoom;
    QtConcurrent::run([this, room]() {
        QVector<DevicePlugStatus> statuses;
        bool ok = m_ipc.queryRoomStatus(room, statuses);
        // 回到主线程
        QMetaObject::invokeMethod(this, [this, ok, statuses, room]() {
            // 期间房间可能已切换，检查一致性
            if (room != m_currentRoom) return;
            if (!ok) {
                m_table->setRowCount(0);
                return;
            }
            // 复用 onRefreshStatus 的填充逻辑（重新调用以保持一致）
            // 这里简化为重新同步调用一次（数据已在手）
            m_table->setRowCount(statuses.size());
            for (int i = 0; i < statuses.size(); ++i) {
                const DevicePlugStatus& s = statuses[i];
                m_table->setItem(i, 0, new QTableWidgetItem(QString::number(s.deviceId)));
                m_table->setItem(i, 1, new QTableWidgetItem(s.name));
                m_table->setItem(i, 2, new QTableWidgetItem(s.online ? "在线" : "离线"));
                m_table->setItem(i, 3, new QTableWidgetItem(s.isOn ? "开" : "关"));
                m_table->setItem(i, 4, new QTableWidgetItem(QString::number(s.powerW, 'f', 1)));
                m_table->setItem(i, 5, new QTableWidgetItem(QString::number(s.energyKwh, 'f', 3)));
                m_table->setItem(i, 6, new QTableWidgetItem(QString::number(s.temperature)));
                m_table->setItem(i, 7, new QTableWidgetItem(QString::number(s.countdownLeftMin)));
                m_table->setItem(i, 8, new QTableWidgetItem(s.faultDesc));

                // 操作列：断电 + 删除两个按钮
                QWidget* opWidget = new QWidget();
                QHBoxLayout* opLayout = new QHBoxLayout(opWidget);
                opLayout->setContentsMargins(2, 2, 2, 2);
                opLayout->setSpacing(4);

                QPushButton* powerOffBtn = new QPushButton("断电");
                powerOffBtn->setStyleSheet(
                    "QPushButton { background-color: #c0392b; color: white; border-radius: 6px; padding: 6px; }"
                    "QPushButton:pressed { background-color: #922b21; }");
                powerOffBtn->setProperty("row", i);
                connect(powerOffBtn, &QPushButton::clicked, this, [this, powerOffBtn]() {
                    int row = powerOffBtn->property("row").toInt();
                    onPowerOffClicked(row);
                });

                QPushButton* deleteBtn = new QPushButton("删除");
                deleteBtn->setStyleSheet(
                    "QPushButton { background-color: #7f8c8d; color: white; border-radius: 6px; padding: 6px; }"
                    "QPushButton:pressed { background-color: #566573; }");
                deleteBtn->setProperty("room_id", s.roomId);
                deleteBtn->setProperty("device_id", s.deviceId);
                deleteBtn->setProperty("name", s.name);
                connect(deleteBtn, &QPushButton::clicked, this, [this, deleteBtn]() {
                    int roomId = deleteBtn->property("room_id").toInt();
                    int devId = deleteBtn->property("device_id").toInt();
                    QString name = deleteBtn->property("name").toString();
                    onDeletePlugClicked(roomId, devId, name);
                });

                opLayout->addWidget(powerOffBtn);
                opLayout->addWidget(deleteBtn);
                m_table->setCellWidget(i, 9, opWidget);
            }
        });
    });
}

// ================================================================
// 自动刷新（定时器槽）—— 同时刷新插座和 ESP32
// ================================================================
void DeviceManageWindow::onAutoRefresh()
{
    refreshStatusAsync();
    // ESP32 列表只在 ESP32 Tab 可见时刷新
    if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        onRefreshEsp32();
    }
}

// ================================================================
// 新增插座：显示内嵌表单面板（不使用模态对话框）
// ================================================================
void DeviceManageWindow::onAddPlugClicked()
{
    showAddPlugPanel(true);
}

// ================================================================
// 确认添加插座
// ================================================================
void DeviceManageWindow::onConfirmAddPlug()
{
    QString name  = m_nameEdit->text().trimmed();
    QString ip    = m_ipEdit->text().trimmed();
    QString token = m_tokenEdit->text().trimmed();
    int room_id   = m_addRoomCombo->currentData().toInt();
    int device_id = m_addDeviceCombo->currentData().toInt();

    if (name.isEmpty() || ip.isEmpty() || token.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写完整信息");
        return;
    }
    if (token.length() != 32) {
        QMessageBox::warning(this, "提示", "Token 应为 32 位 hex 字符串");
        return;
    }

    if (m_ipc.addPlug(room_id, device_id, name, ip, token)) {
        showAddPlugPanel(false);
        QMessageBox::information(this, "成功", "插座添加成功");
        m_currentRoom = room_id;
        m_roomCombo->setCurrentIndex(room_id);
        onRefreshStatus();
    } else {
        QMessageBox::warning(this, "失败", "插座添加失败，请检查 device_process 是否运行");
    }
}

// ================================================================
// 取消添加
// ================================================================
void DeviceManageWindow::onCancelAddPlug()
{
    showAddPlugPanel(false);
}

// ================================================================
// 手动断电按钮（弹确认对话框，然后经主进程中转）
// ================================================================
void DeviceManageWindow::onPowerOffClicked(int row)
{
    int device_id = m_table->item(row, 0)->text().toInt();
    QString name  = m_table->item(row, 1)->text();
    int room_id   = m_currentRoom;

    auto ret = QMessageBox::question(
        this, "确认断电",
        QString("确定要关闭插座吗？\n房间 %1 设备 %2 (%3)\n\n这会提前结束当前使用。")
            .arg(room_id).arg(device_id).arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    // 经主进程中转
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_ipc.requestPowerOff(room_id, device_id);
    QApplication::restoreOverrideCursor();

    if (ok) {
        QMessageBox::information(this, "成功", "已发送断电指令");
        onRefreshStatus();
    } else {
        QMessageBox::warning(this, "失败", "断电失败，请检查主进程是否运行");
    }
}

// ================================================================
// 删除插座按钮（弹确认对话框，直接发 IPC 给 device_process）
// ================================================================
void DeviceManageWindow::onDeletePlugClicked(int room_id, int device_id, const QString& name)
{
    auto ret = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除插座吗？\n房间 %1 设备 %2 (%3)\n\n删除后不可恢复。")
            .arg(room_id).arg(device_id).arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_ipc.removePlug(room_id, device_id);
    QApplication::restoreOverrideCursor();

    if (ok) {
        QMessageBox::information(this, "成功", "插座已删除");
        onRefreshStatus();
    } else {
        QMessageBox::warning(this, "失败", "删除失败，请检查 device_process 是否运行");
    }
}

// ================================================================
// 返回
// ================================================================
void DeviceManageWindow::onBack()
{
    emit backToHome();
}

// ================================================================
// ========== ESP32 Tab 相关槽函数 ==========
// ================================================================

// ================================================================
// onRefreshEsp32：刷新 ESP32 列表
// ================================================================
void DeviceManageWindow::onRefreshEsp32()
{
    QVector<Esp32Status> statuses;
    if (!m_ipc.listEsp32(statuses)) {
        qDebug() << "[DeviceManage] listEsp32 failed (device_process 未启动?)";
        m_esp32Table->setRowCount(0);
        return;
    }

    m_esp32Table->setRowCount(statuses.size());

    for (int i = 0; i < statuses.size(); ++i) {
        const Esp32Status& s = statuses[i];

        m_esp32Table->setItem(i, 0, new QTableWidgetItem(QString::number(s.roomId)));
        m_esp32Table->setItem(i, 1, new QTableWidgetItem(s.name));
        m_esp32Table->setItem(i, 2, new QTableWidgetItem(s.esp32Ip));
        m_esp32Table->setItem(i, 3, new QTableWidgetItem(s.rtspUrl));
        m_esp32Table->setItem(i, 4, new QTableWidgetItem(s.online ? "在线" : "离线"));

        // 操作列：删除按钮
        QWidget* opWidget = new QWidget();
        QHBoxLayout* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(2, 2, 2, 2);
        opLayout->setSpacing(4);

        QPushButton* deleteBtn = new QPushButton("删除");
        deleteBtn->setStyleSheet(
            "QPushButton { background-color: #7f8c8d; color: white; border-radius: 6px; padding: 6px; }"
            "QPushButton:pressed { background-color: #566573; }");
        deleteBtn->setProperty("room_id", s.roomId);
        deleteBtn->setProperty("name", s.name);
        connect(deleteBtn, &QPushButton::clicked, this, [this, deleteBtn]() {
            int roomId = deleteBtn->property("room_id").toInt();
            QString name = deleteBtn->property("name").toString();
            onDeleteEsp32Clicked(roomId, name);
        });

        opLayout->addWidget(deleteBtn);
        m_esp32Table->setCellWidget(i, 5, opWidget);
    }

    m_esp32Table->setColumnWidth(0, 70);
    m_esp32Table->setColumnWidth(1, 130);
    m_esp32Table->setColumnWidth(2, 140);
    m_esp32Table->setColumnWidth(3, 250);
    m_esp32Table->setColumnWidth(4, 60);
    m_esp32Table->setColumnWidth(5, 100);
}

// ================================================================
// onAddEsp32Clicked：显示新增 ESP32 面板
// ================================================================
void DeviceManageWindow::onAddEsp32Clicked()
{
    showAddEsp32Panel(true);
}

// ================================================================
// onConfirmAddEsp32：确认添加 ESP32
// ================================================================
void DeviceManageWindow::onConfirmAddEsp32()
{
    QString name    = m_esp32NameEdit->text().trimmed();
    QString ip      = m_esp32IpEdit->text().trimmed();
    QString rtspUrl = m_esp32RtspEdit->text().trimmed();
    int room_id     = m_esp32RoomCombo->currentData().toInt();

    if (name.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写名称");
        return;
    }
    if (rtspUrl.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 RTSP URL 或本地视频路径");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_ipc.addEsp32(room_id, name, ip, rtspUrl);
    QApplication::restoreOverrideCursor();

    if (ok) {
        showAddEsp32Panel(false);
        QMessageBox::information(this, "成功", "ESP32 添加成功");
        onRefreshEsp32();
    } else {
        QMessageBox::warning(this, "失败", "ESP32 添加失败，请检查 device_process 是否运行");
    }
}

// ================================================================
// onCancelAddEsp32：取消添加
// ================================================================
void DeviceManageWindow::onCancelAddEsp32()
{
    showAddEsp32Panel(false);
}

// ================================================================
// onDeleteEsp32Clicked：删除 ESP32
// ================================================================
void DeviceManageWindow::onDeleteEsp32Clicked(int room_id, const QString& name)
{
    auto ret = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除 ESP32 吗？\n房间 %1 (%2)\n\n删除后不可恢复。")
            .arg(room_id).arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_ipc.removeEsp32(room_id);
    QApplication::restoreOverrideCursor();

    if (ok) {
        QMessageBox::information(this, "成功", "ESP32 已删除");
        onRefreshEsp32();
    } else {
        QMessageBox::warning(this, "失败", "删除失败，请检查 device_process 是否运行");
    }
}
