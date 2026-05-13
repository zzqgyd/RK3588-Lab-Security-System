#include "statistics_window.h"
#include "db/db_manager.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>
#include <QSet>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QChart>

QT_CHARTS_USE_NAMESPACE

// ============================================================
// 构造函数
// ============================================================
StatisticsWindow::StatisticsWindow(QWidget *parent)
    : QWidget(parent)
    , m_tabWidget(nullptr)
    , m_attendancePage(nullptr)
    , m_startDateEdit(nullptr)
    , m_endDateEdit(nullptr)
    , m_groupByCombo(nullptr)
    , m_roomFilterCombo(nullptr)
    , m_attendanceChartView(nullptr)
    , m_roomTotalLabel(nullptr)
    , m_refreshBtn(nullptr)
    , m_backBtn(nullptr)
    , m_devicePage(nullptr)
    , m_deviceStartDate(nullptr)
    , m_deviceEndDate(nullptr)
    , m_deviceRoomFilterCombo(nullptr)
    , m_deviceChartView(nullptr)
    , m_roomTotal(0)
{
    m_colors = {
        QColor("#3498db"), QColor("#e74c3c"), QColor("#2ecc71"),
        QColor("#f39c12"), QColor("#9b59b6"), QColor("#1abc9c"),
        QColor("#e67e22"), QColor("#95a5a6")
    };
    
    setupUI();
    setWindowTitle("数据统计");
    setMinimumSize(1024, 580);
    setAttribute(Qt::WA_DeleteOnClose);
}

StatisticsWindow::~StatisticsWindow()
{
}

// ============================================================
// 创建界面
// ============================================================
void StatisticsWindow::setupUI()
{
    setStyleSheet("background-color: #2c3e50;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // ============================================================
    // 标题栏（含返回按钮）
    // ============================================================
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);

    m_backBtn = new QPushButton("← 返回");
    m_backBtn->setFixedSize(90, 35);
    m_backBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #7f8c8d;"
        "    color: white;"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "    border-radius: 6px;"
        "}"
        "QPushButton:hover { background-color: #95a5a6; }"
        "QPushButton:pressed { background-color: #6c7a7a; }"
    );
    connect(m_backBtn, &QPushButton::clicked, this, &StatisticsWindow::backToHome);

    titleLayout->addWidget(m_backBtn);
    titleLayout->addStretch();

    QLabel* title = new QLabel("📊 数据统计");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: #3498db;");
    titleLayout->addWidget(title);

    titleLayout->addStretch();
    QLabel* placeholder = new QLabel("");
    placeholder->setFixedWidth(90);
    titleLayout->addWidget(placeholder);

    mainLayout->addLayout(titleLayout);
    
    // ============================================================
    // 选项卡
    // ============================================================
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { background-color: #34495e; border-radius: 6px; }"
        "QTabBar::tab { background-color: #2c3e50; color: white; padding: 8px 25px; "
        "              font-size: 14px; font-weight: bold; margin-right: 3px; border-radius: 6px 6px 0 0; }"
        "QTabBar::tab:selected { background-color: #3498db; }"
        "QTabBar::tab:hover { background-color: #2980b9; }"
    );
    
    // ============================================================
    // 页面1：考勤统计
    // ============================================================
    m_attendancePage = new QWidget();
    QVBoxLayout* attendanceLayout = new QVBoxLayout(m_attendancePage);
    attendanceLayout->setSpacing(10);
    attendanceLayout->setContentsMargins(10, 10, 10, 10);
    
    // 筛选栏
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(10);
    
    QLabel* startLabel = new QLabel("从:");
    startLabel->setStyleSheet("color: white; font-size: 13px;");
    startLabel->setFixedWidth(25);
    m_startDateEdit = new QDateEdit();
    m_startDateEdit->setDate(QDate::currentDate().addDays(-30));
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_startDateEdit->setFixedHeight(32);
    m_startDateEdit->setFixedWidth(120);
    m_startDateEdit->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_startDateEdit, &QDateEdit::dateChanged, this, &StatisticsWindow::onDateRangeChanged);
    
    QLabel* endLabel = new QLabel("到:");
    endLabel->setStyleSheet("color: white; font-size: 13px;");
    endLabel->setFixedWidth(25);
    m_endDateEdit = new QDateEdit();
    m_endDateEdit->setDate(QDate::currentDate());
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_endDateEdit->setFixedHeight(32);
    m_endDateEdit->setFixedWidth(120);
    m_endDateEdit->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_endDateEdit, &QDateEdit::dateChanged, this, &StatisticsWindow::onDateRangeChanged);
    
    QLabel* groupLabel = new QLabel("周期:");
    groupLabel->setStyleSheet("color: white; font-size: 13px;");
    groupLabel->setFixedWidth(35);
    m_groupByCombo = new QComboBox();
    m_groupByCombo->addItem("按年");
    m_groupByCombo->addItem("按月");
    m_groupByCombo->addItem("按周");
    m_groupByCombo->setFixedHeight(32);
    m_groupByCombo->setFixedWidth(90);
    m_groupByCombo->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_groupByCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatisticsWindow::onGroupByChanged);
    
    QLabel* roomLabel = new QLabel("房间:");
    roomLabel->setStyleSheet("color: white; font-size: 13px;");
    roomLabel->setFixedWidth(35);
    m_roomFilterCombo = new QComboBox();
    m_roomFilterCombo->addItem("全部");
    for (int i = 1; i <= 8; i++) {
        m_roomFilterCombo->addItem(QString("房间%1").arg(i));
    }
    m_roomFilterCombo->setFixedHeight(32);
    m_roomFilterCombo->setFixedWidth(80);
    m_roomFilterCombo->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_roomFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatisticsWindow::onRoomFilterChanged);
    
    m_refreshBtn = new QPushButton("🔄");
    m_refreshBtn->setFixedSize(32, 32);
    m_refreshBtn->setToolTip("刷新数据");
    m_refreshBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    connect(m_refreshBtn, &QPushButton::clicked, this, &StatisticsWindow::onRefresh);

    filterLayout->addWidget(startLabel);
    filterLayout->addWidget(m_startDateEdit);
    filterLayout->addWidget(endLabel);
    filterLayout->addWidget(m_endDateEdit);
    filterLayout->addWidget(groupLabel);
    filterLayout->addWidget(m_groupByCombo);
    filterLayout->addWidget(roomLabel);
    filterLayout->addWidget(m_roomFilterCombo);
    filterLayout->addStretch();
    filterLayout->addWidget(m_refreshBtn);

    attendanceLayout->addLayout(filterLayout);

    // 图表 + 侧面房间人数
    QHBoxLayout* chartLayout = new QHBoxLayout();
    chartLayout->setSpacing(10);

    m_attendanceChartView = new QChartView();
    m_attendanceChartView->setRenderHint(QPainter::Antialiasing);
    m_attendanceChartView->setStyleSheet("background-color: white; border-radius: 6px;");
    chartLayout->addWidget(m_attendanceChartView, 1);

    // 侧面房间人数显示
    m_roomTotalLabel = new QLabel();
    m_roomTotalLabel->setFixedWidth(140);
    m_roomTotalLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_roomTotalLabel->setStyleSheet(
        "QLabel {"
        "    background-color: #34495e;"
        "    color: white;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "    border-radius: 6px;"
        "    padding: 15px;"
        "}"
    );
    m_roomTotalLabel->setText("房间人数\n加载中...");
    chartLayout->addWidget(m_roomTotalLabel);

    attendanceLayout->addLayout(chartLayout, 1);

    m_tabWidget->addTab(m_attendancePage, "📈 考勤统计");
    
    // ============================================================
    // 页面2：设备使用统计
    // ============================================================
    m_devicePage = new QWidget();
    QVBoxLayout* deviceLayout = new QVBoxLayout(m_devicePage);
    deviceLayout->setSpacing(10);
    deviceLayout->setContentsMargins(10, 10, 10, 10);
    
    QHBoxLayout* deviceFilterLayout = new QHBoxLayout();
    deviceFilterLayout->setSpacing(10);
    
    QLabel* devStartLabel = new QLabel("从:");
    devStartLabel->setStyleSheet("color: white; font-size: 13px;");
    devStartLabel->setFixedWidth(25);
    m_deviceStartDate = new QDateEdit();
    m_deviceStartDate->setDate(QDate::currentDate().addDays(-30));
    m_deviceStartDate->setCalendarPopup(true);
    m_deviceStartDate->setDisplayFormat("yyyy-MM-dd");
    m_deviceStartDate->setFixedHeight(32);
    m_deviceStartDate->setFixedWidth(120);
    m_deviceStartDate->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_deviceStartDate, &QDateEdit::dateChanged, this, &StatisticsWindow::onDateRangeChanged);
    
    QLabel* devEndLabel = new QLabel("到:");
    devEndLabel->setStyleSheet("color: white; font-size: 13px;");
    devEndLabel->setFixedWidth(25);
    m_deviceEndDate = new QDateEdit();
    m_deviceEndDate->setDate(QDate::currentDate());
    m_deviceEndDate->setCalendarPopup(true);
    m_deviceEndDate->setDisplayFormat("yyyy-MM-dd");
    m_deviceEndDate->setFixedHeight(32);
    m_deviceEndDate->setFixedWidth(120);
    m_deviceEndDate->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_deviceEndDate, &QDateEdit::dateChanged, this, &StatisticsWindow::onDateRangeChanged);
    
    QLabel* devRoomLabel = new QLabel("房间:");
    devRoomLabel->setStyleSheet("color: white; font-size: 13px;");
    devRoomLabel->setFixedWidth(35);
    m_deviceRoomFilterCombo = new QComboBox();
    m_deviceRoomFilterCombo->addItem("全部");
    for (int i = 1; i <= 8; i++) {
        m_deviceRoomFilterCombo->addItem(QString("房间%1").arg(i));
    }
    m_deviceRoomFilterCombo->setFixedHeight(32);
    m_deviceRoomFilterCombo->setFixedWidth(80);
    m_deviceRoomFilterCombo->setStyleSheet("background-color: white; padding: 5px; border-radius: 4px; font-size: 13px;");
    connect(m_deviceRoomFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatisticsWindow::onRoomFilterChanged);
    
    deviceFilterLayout->addWidget(devStartLabel);
    deviceFilterLayout->addWidget(m_deviceStartDate);
    deviceFilterLayout->addWidget(devEndLabel);
    deviceFilterLayout->addWidget(m_deviceEndDate);
    deviceFilterLayout->addWidget(devRoomLabel);
    deviceFilterLayout->addWidget(m_deviceRoomFilterCombo);
    deviceFilterLayout->addStretch();
    
    deviceLayout->addLayout(deviceFilterLayout);
    
    m_deviceChartView = new QChartView();
    m_deviceChartView->setRenderHint(QPainter::Antialiasing);
    m_deviceChartView->setStyleSheet("background-color: white; border-radius: 6px;");
    deviceLayout->addWidget(m_deviceChartView, 1);
    
    m_tabWidget->addTab(m_devicePage, "⚙️ 设备统计");
    
    mainLayout->addWidget(m_tabWidget);
}

// ============================================================
// 分组键值
// ============================================================
QString StatisticsWindow::getGroupKey(const QDate& date, int groupBy)
{
    switch (groupBy) {
        case 0: return QString::number(date.year());
        case 1: return QString("%1年%2月").arg(date.year()).arg(date.month(), 2, 10, QChar('0'));
        case 2: default: {
            int week = date.weekNumber();
            return QString("%1年第%2周").arg(date.year()).arg(week);
        }
    }
}

// ============================================================
// 加载考勤统计
// 三类：签到且签退 / 签到没签退 / 没签到，按时间分组，按房间筛选
// 数据查询委托 DbManager，本函数只做分组与分类计算
// ============================================================
void StatisticsWindow::loadAttendanceStats()
{
    QDate start = m_startDateEdit->date();
    QDate end = m_endDateEdit->date();
    int groupBy = m_groupByCombo->currentIndex();
    int roomFilter = m_roomFilterCombo->currentIndex();  // 0=全部, 1..8 → room_id 0..7

    // 转换：0 → -1（全部），否则 room_id = roomFilter - 1
    int roomId = (roomFilter > 0) ? (roomFilter - 1) : -1;

    m_signInAndOutData.clear();
    m_signInOnlyData.clear();
    m_notSignInData.clear();

    // 1. 已录入人员总数
    m_roomTotal = DbManager::instance().countRoomMembers(roomId);

    // 2. 考勤记录
    QVector<AttendanceRecord> records =
        DbManager::instance().queryAttendance(start, end, roomId);

    // 3. 按时间分组记录签到/签退人员集合
    QMap<QString, QSet<QString>> signInNames;
    QMap<QString, QSet<QString>> signOutNames;

    for (const AttendanceRecord& r : records) {
        QDate date = QDate::fromString(r.time.left(10), "yyyy-MM-dd");
        if (!date.isValid()) continue;
        QString key = getGroupKey(date, groupBy);

        if (r.type == QStringLiteral("签到")) {
            signInNames[key].insert(r.personName);
        } else if (r.type == QStringLiteral("签退")) {
            signOutNames[key].insert(r.personName);
        }
    }

    // 4. 每个时间分组计算三类
    QStringList allKeys = signInNames.keys() + signOutNames.keys();
    allKeys.removeDuplicates();
    std::sort(allKeys.begin(), allKeys.end());

    for (const QString& key : allKeys) {
        const QSet<QString>& ins = signInNames.value(key);
        const QSet<QString>& outs = signOutNames.value(key);

        int signInAndOut = 0;   // 签到且签退
        for (const QString& n : ins) {
            if (outs.contains(n)) signInAndOut++;
        }
        int signInOnly = ins.size() - signInAndOut;   // 签到没签退
        int notSignIn = m_roomTotal - ins.size();      // 没签到
        if (notSignIn < 0) notSignIn = 0;

        m_signInAndOutData[key] = signInAndOut;
        m_signInOnlyData[key] = signInOnly;
        m_notSignInData[key] = notSignIn;
    }

    updateRoomTotalLabel(roomFilter);
    updateAttendanceChart();
}

// ============================================================
// 更新侧面房间人数标签
// ============================================================
void StatisticsWindow::updateRoomTotalLabel(int roomFilter)
{
    if (!m_roomTotalLabel) return;
    QString roomText = (roomFilter > 0)
        ? QString("房间%1").arg(roomFilter)
        : QStringLiteral("全部房间");
    m_roomTotalLabel->setText(QString("%1\n\n人数: %2").arg(roomText).arg(m_roomTotal));
}

// ============================================================
// 加载设备使用统计
// 数据查询委托 DbManager
// ============================================================
void StatisticsWindow::loadDeviceUsageStats()
{
    QDate start = m_deviceStartDate->date();
    QDate end = m_deviceEndDate->date();
    int roomFilter = m_deviceRoomFilterCombo->currentIndex();

    // 转换：0 → -1（全部），否则 room_id = roomFilter - 1
    int roomId = (roomFilter > 0) ? (roomFilter - 1) : -1;

    m_deviceData = DbManager::instance().queryDeviceUsageSummary(start, end, roomId);

    updateDeviceUsageChart();
}

// ============================================================
// 更新考勤柱状图（三类：签到且签退 / 签到没签退 / 没签到）
// ============================================================
void StatisticsWindow::updateAttendanceChart()
{
    if (m_signInAndOutData.isEmpty() && m_signInOnlyData.isEmpty() && m_notSignInData.isEmpty()) {
        QChart* chart = new QChart();
        chart->setTitle("暂无考勤数据");
        chart->setAnimationOptions(QChart::SeriesAnimations);
        chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
        m_attendanceChartView->setChart(chart);
        return;
    }

    // 汇总所有时间分组键并排序
    QStringList keys = m_signInAndOutData.keys();
    keys += m_signInOnlyData.keys();
    keys += m_notSignInData.keys();
    keys.removeDuplicates();
    std::sort(keys.begin(), keys.end());

    // 创建三类数据集
    QBarSet* signInAndOutSet = new QBarSet("✅签到且签退");
    QBarSet* signInOnlySet   = new QBarSet("⚠️签到没签退");
    QBarSet* notSignInSet    = new QBarSet("❌没签到");
    signInAndOutSet->setColor(QColor("#27ae60"));   // 绿
    signInOnlySet->setColor(QColor("#f39c12"));     // 橙
    notSignInSet->setColor(QColor("#e74c3c"));      // 红

    int maxVal = 0;
    for (const QString& key : keys) {
        int both  = m_signInAndOutData.value(key, 0);
        int only  = m_signInOnlyData.value(key, 0);
        int none  = m_notSignInData.value(key, 0);
        signInAndOutSet->append(both);
        signInOnlySet->append(only);
        notSignInSet->append(none);
        maxVal = qMax(maxVal, qMax(both, qMax(only, none)));
    }

    QBarSeries* series = new QBarSeries();
    series->append(signInAndOutSet);
    series->append(signInOnlySet);
    series->append(notSignInSet);
    series->setLabelsVisible(true);
    series->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);

    QChart* chart = new QChart();
    chart->addSeries(series);
    chart->setTitle("考勤统计（按房间筛选）");
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
    chart->setTheme(QChart::ChartThemeLight);

    // X轴
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(keys);
    axisX->setTitleText("时间");
    axisX->setLabelsAngle(-30);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    // Y轴
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("人数");
    axisY->setRange(0, maxVal + 2);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    m_attendanceChartView->setChart(chart);
}

// ============================================================
// 更新设备使用饼图
// ============================================================
void StatisticsWindow::updateDeviceUsageChart()
{
    if (m_deviceData.isEmpty()) {
        QChart* chart = new QChart();
        chart->setTitle("暂无设备使用数据");
        chart->setAnimationOptions(QChart::SeriesAnimations);
        chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
        m_deviceChartView->setChart(chart);
        return;
    }
    
    QPieSeries* series = new QPieSeries();
    series->setLabelsVisible(true);
    series->setLabelsPosition(QPieSlice::LabelOutside);
    series->setPieSize(0.7);
    
    int idx = 0;
    int total = 0;
    for (int v : m_deviceData.values()) total += v;

    for (auto it = m_deviceData.begin(); it != m_deviceData.end(); ++it) {
        int deviceId = it.key();
        int minutes = it.value();
        float percent = total > 0 ? (minutes * 100.0f / total) : 0;

        QPieSlice* slice = series->append(QString("设备%1").arg(deviceId), minutes);
        slice->setColor(m_colors[idx % m_colors.size()]);
        slice->setLabel(QString("设备%1\n%2分钟\n(%3%)")
            .arg(deviceId).arg(minutes).arg(percent, 0, 'f', 1));
        slice->setLabelFont(QFont("Arial", 10));
        if (minutes == m_deviceData.last()) {
            slice->setExploded(true);
        }
        idx++;
    }
    
    QChart* chart = new QChart();
    chart->addSeries(series);
    QString roomText = m_deviceRoomFilterCombo->currentText();
    chart->setTitle(QString("设备使用统计（%1）").arg(roomText));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
    chart->setTheme(QChart::ChartThemeLight);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);
    
    m_deviceChartView->setChart(chart);
}

// ============================================================
// 刷新数据
// ============================================================
void StatisticsWindow::onRefresh()
{
    loadAttendanceStats();
    loadDeviceUsageStats();
}

// ============================================================
// 日期范围改变
// ============================================================
void StatisticsWindow::onDateRangeChanged()
{
    if (m_tabWidget->currentIndex() == 0) {
        loadAttendanceStats();
    } else {
        loadDeviceUsageStats();
    }
}

// ============================================================
// 分组方式改变
// ============================================================
void StatisticsWindow::onGroupByChanged(int index)
{
    Q_UNUSED(index);
    loadAttendanceStats();
}

// ============================================================
// 房间号筛选改变
// ============================================================
void StatisticsWindow::onRoomFilterChanged(int index)
{
    Q_UNUSED(index);
    if (m_tabWidget->currentIndex() == 0) {
        loadAttendanceStats();
    } else {
        loadDeviceUsageStats();
    }
}

// ============================================================
// 选项卡切换
// ============================================================
void StatisticsWindow::onTabChanged(int index)
{
    if (index == 0) {
        loadAttendanceStats();
    } else {
        loadDeviceUsageStats();
    }
}

// ============================================================
// 窗口显示事件
// ============================================================
void StatisticsWindow::showEvent(QShowEvent *event)
{
    loadAttendanceStats();
    loadDeviceUsageStats();
    QWidget::showEvent(event);
}