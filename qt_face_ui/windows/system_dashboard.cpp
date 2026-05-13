#include "system_dashboard.h"
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegExp>
#include <QDebug>

SystemDashboard::SystemDashboard(QWidget *parent)
    : QWidget(parent)
    , m_memoryBar(nullptr)
    , m_memoryLabel(nullptr)
    , m_memoryDetailLabel(nullptr)
    , m_ddrBar(nullptr)
    , m_ddrFreqLabel(nullptr)
    , m_ddrLoadLabel(nullptr)
    , m_gpuBar(nullptr)
    , m_gpuFreqLabel(nullptr)
    , m_gpuLoadLabel(nullptr)
    , m_vpuDecLabel(nullptr)
    , m_vpuEncLabel(nullptr)
    , m_tempLabel(nullptr)
    , m_timer(nullptr)
    , m_firstCpuRead(true)
{
    // 初始化 CPU 相关
    m_cpuBars.resize(8);
    m_cpuLabels.resize(8);
    m_prevUser.resize(8);
    m_prevNice.resize(8);
    m_prevSystem.resize(8);
    m_prevIdle.resize(8);
    
    // 初始化 NPU 相关
    m_npuBars.resize(3);
    m_npuLabels.resize(3);
    
    // 初始化 RGA 相关
    m_rgaBars.resize(3);
    m_rgaLabels.resize(3);
    
    // ============================================================
    // 创建 UI
    // ============================================================
    setStyleSheet("background-color: #2c3e50;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    mainLayout->setSpacing(10);
    
    // ---- 标题栏 ----
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);
    
    QPushButton* backBtn = new QPushButton("← 返回");
    backBtn->setFixedSize(80, 32);
    backBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-size: 13px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #95a5a6; }"
    );
    connect(backBtn, &QPushButton::clicked, this, &SystemDashboard::backToHome);
    titleLayout->addWidget(backBtn);
    
    titleLayout->addStretch();
    
    QLabel* title = new QLabel("系统状态仪表盘");
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: #3498db;");
    titleLayout->addWidget(title);
    
    titleLayout->addStretch();
    
    m_tempLabel = new QLabel("0°C");
    m_tempLabel->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #2ecc71;"
        "background-color: rgba(52, 73, 94, 0.5); padding: 4px 14px; border-radius: 6px;"
    );
    titleLayout->addWidget(m_tempLabel);
    mainLayout->addLayout(titleLayout);
    
    // ---- 分隔线 ----
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #34495e;");
    mainLayout->addWidget(line);
    
    // ---- 网格布局 ----
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(8);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    
    int row = 0;
    
    // ============================================================
    // 1. CPU 8个核心 (第0-1行)
    // ============================================================
    QLabel* cpuTitle = new QLabel("CPU 使用率");
    cpuTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
    grid->addWidget(cpuTitle, row, 0, 1, 4);
    row++;
    
    for (int i = 0; i < 8; i++) {
        int col = i % 4;
        int r = row + i / 4;
        
        QVBoxLayout* cpuLayout = new QVBoxLayout();
        cpuLayout->setSpacing(1);
        
        QLabel* nameLabel = new QLabel(QString("CPU%1").arg(i));
        nameLabel->setStyleSheet("font-size: 10px; color: #bdc3c7;");
        cpuLayout->addWidget(nameLabel);
        
        m_cpuBars[i] = new QProgressBar();
        m_cpuBars[i]->setRange(0, 100);
        m_cpuBars[i]->setTextVisible(false);
        m_cpuBars[i]->setFixedHeight(10);
        m_cpuBars[i]->setStyleSheet(
            "QProgressBar { background-color: #34495e; border-radius: 2px; height: 10px; }"
            "QProgressBar::chunk { border-radius: 2px; background-color: #3498db; }"
        );
        cpuLayout->addWidget(m_cpuBars[i]);
        
        m_cpuLabels[i] = new QLabel("0%");
        m_cpuLabels[i]->setAlignment(Qt::AlignRight);
        m_cpuLabels[i]->setStyleSheet("font-size: 9px; color: #bdc3c7;");
        cpuLayout->addWidget(m_cpuLabels[i]);
        
        QWidget* w = new QWidget();
        w->setLayout(cpuLayout);
        grid->addWidget(w, r, col);
    }
    row += 2;
    
    // ---- 分隔线 ----
    QFrame* line2 = new QFrame();
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet("color: #34495e;");
    grid->addWidget(line2, row, 0, 1, 4);
    row++;
    
    // ============================================================
    // 2. 内存 (行row, 列0-1)
    // ============================================================
    QLabel* memTitle = new QLabel("内存使用");
    memTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #2ecc71;");
    grid->addWidget(memTitle, row, 0, 1, 2);
    
    QVBoxLayout* memLayout = new QVBoxLayout();
    memLayout->setSpacing(1);
    
    m_memoryBar = new QProgressBar();
    m_memoryBar->setRange(0, 100);
    m_memoryBar->setTextVisible(false);
    m_memoryBar->setFixedHeight(14);
    m_memoryBar->setStyleSheet(
        "QProgressBar { background-color: #34495e; border-radius: 3px; height: 14px; }"
        "QProgressBar::chunk { border-radius: 3px; background-color: #2ecc71; }"
    );
    memLayout->addWidget(m_memoryBar);
    
    m_memoryLabel = new QLabel("0%");
    m_memoryLabel->setAlignment(Qt::AlignRight);
    m_memoryLabel->setStyleSheet("font-size: 11px; color: #bdc3c7;");
    memLayout->addWidget(m_memoryLabel);
    
    m_memoryDetailLabel = new QLabel("已用: 0 MB / 总计: 0 MB");
    m_memoryDetailLabel->setStyleSheet("font-size: 10px; color: #95a5a6;");
    memLayout->addWidget(m_memoryDetailLabel);
    
    QWidget* memWidget = new QWidget();
    memWidget->setLayout(memLayout);
    grid->addWidget(memWidget, row + 1, 0, 1, 2);
    
    // ============================================================
    // 3. NPU 3个核心 (行row, 列2-3)
    // ============================================================
    QLabel* npuTitle = new QLabel("NPU 使用率");
    npuTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #9b59b6;");
    grid->addWidget(npuTitle, row, 2, 1, 2);
    
    for (int i = 0; i < 3; i++) {
        QVBoxLayout* npuLayout = new QVBoxLayout();
        npuLayout->setSpacing(1);
        
        QLabel* nameLabel = new QLabel(QString("NPU%1").arg(i));
        nameLabel->setStyleSheet("font-size: 10px; color: #bdc3c7;");
        npuLayout->addWidget(nameLabel);
        
        m_npuBars[i] = new QProgressBar();
        m_npuBars[i]->setRange(0, 100);
        m_npuBars[i]->setTextVisible(false);
        m_npuBars[i]->setFixedHeight(10);
        m_npuBars[i]->setStyleSheet(
            "QProgressBar { background-color: #34495e; border-radius: 2px; height: 10px; }"
            "QProgressBar::chunk { border-radius: 2px; background-color: #9b59b6; }"
        );
        npuLayout->addWidget(m_npuBars[i]);
        
        m_npuLabels[i] = new QLabel("N/A");
        m_npuLabels[i]->setAlignment(Qt::AlignRight);
        m_npuLabels[i]->setStyleSheet("font-size: 9px; color: #95a5a6;");
        npuLayout->addWidget(m_npuLabels[i]);
        
        QWidget* w = new QWidget();
        w->setLayout(npuLayout);
        grid->addWidget(w, row + 1, 2 + i);
    }
    row += 2;
    
    // ---- 分隔线 ----
    QFrame* line3 = new QFrame();
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet("color: #34495e;");
    grid->addWidget(line3, row, 0, 1, 4);
    row++;
    
    // ============================================================
    // 4. RGA 3个核心 (行row, 列0-2)
    // ============================================================
    QLabel* rgaTitle = new QLabel("RGA 负载");
    rgaTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #1abc9c;");
    grid->addWidget(rgaTitle, row, 0, 1, 3);
    
    for (int i = 0; i < 3; i++) {
        QVBoxLayout* rgaLayout = new QVBoxLayout();
        rgaLayout->setSpacing(1);
        
        QLabel* nameLabel = new QLabel(QString("RGA%1").arg(i));
        nameLabel->setStyleSheet("font-size: 10px; color: #bdc3c7;");
        rgaLayout->addWidget(nameLabel);
        
        m_rgaBars[i] = new QProgressBar();
        m_rgaBars[i]->setRange(0, 100);
        m_rgaBars[i]->setTextVisible(false);
        m_rgaBars[i]->setFixedHeight(10);
        m_rgaBars[i]->setStyleSheet(
            "QProgressBar { background-color: #34495e; border-radius: 2px; height: 10px; }"
            "QProgressBar::chunk { border-radius: 2px; background-color: #1abc9c; }"
        );
        rgaLayout->addWidget(m_rgaBars[i]);
        
        m_rgaLabels[i] = new QLabel("N/A");
        m_rgaLabels[i]->setAlignment(Qt::AlignRight);
        m_rgaLabels[i]->setStyleSheet("font-size: 9px; color: #95a5a6;");
        rgaLayout->addWidget(m_rgaLabels[i]);
        
        QWidget* w = new QWidget();
        w->setLayout(rgaLayout);
        grid->addWidget(w, row + 1, i);
    }
    
    // ============================================================
    // 5. DDR (行row, 列3)
    // ============================================================
    QLabel* ddrTitle = new QLabel("DDR 带宽");
    ddrTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #e67e22;");
    grid->addWidget(ddrTitle, row, 3);
    
    QVBoxLayout* ddrLayout = new QVBoxLayout();
    ddrLayout->setSpacing(1);
    
    m_ddrBar = new QProgressBar();
    m_ddrBar->setRange(0, 100);
    m_ddrBar->setTextVisible(false);
    m_ddrBar->setFixedHeight(14);
    m_ddrBar->setStyleSheet(
        "QProgressBar { background-color: #34495e; border-radius: 3px; height: 14px; }"
        "QProgressBar::chunk { border-radius: 3px; background-color: #e67e22; }"
    );
    ddrLayout->addWidget(m_ddrBar);
    
    m_ddrLoadLabel = new QLabel("负载: 0%");
    m_ddrLoadLabel->setStyleSheet("font-size: 10px; color: #bdc3c7;");
    ddrLayout->addWidget(m_ddrLoadLabel);
    
    m_ddrFreqLabel = new QLabel("频率: 0 MHz");
    m_ddrFreqLabel->setStyleSheet("font-size: 10px; color: #95a5a6;");
    ddrLayout->addWidget(m_ddrFreqLabel);
    
    QWidget* ddrWidget = new QWidget();
    ddrWidget->setLayout(ddrLayout);
    grid->addWidget(ddrWidget, row + 1, 3);
    row += 2;
    
    // ---- 分隔线 ----
    QFrame* line4 = new QFrame();
    line4->setFrameShape(QFrame::HLine);
    line4->setStyleSheet("color: #34495e;");
    grid->addWidget(line4, row, 0, 1, 4);
    row++;
    
    // ============================================================
    // 6. GPU (行row, 列0-1)
    // ============================================================
    QLabel* gpuTitle = new QLabel("GPU");
    gpuTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
    grid->addWidget(gpuTitle, row, 0, 1, 2);
    
    QVBoxLayout* gpuLayout = new QVBoxLayout();
    gpuLayout->setSpacing(1);
    
    m_gpuBar = new QProgressBar();
    m_gpuBar->setRange(0, 100);
    m_gpuBar->setTextVisible(false);
    m_gpuBar->setFixedHeight(14);
    m_gpuBar->setStyleSheet(
        "QProgressBar { background-color: #34495e; border-radius: 3px; height: 14px; }"
        "QProgressBar::chunk { border-radius: 3px; background-color: #e74c3c; }"
    );
    gpuLayout->addWidget(m_gpuBar);
    
    QHBoxLayout* gpuInfoLayout = new QHBoxLayout();
    m_gpuLoadLabel = new QLabel("负载: 0%");
    m_gpuLoadLabel->setStyleSheet("font-size: 10px; color: #bdc3c7;");
    gpuInfoLayout->addWidget(m_gpuLoadLabel);
    gpuInfoLayout->addStretch();
    m_gpuFreqLabel = new QLabel("频率: 0 MHz");
    m_gpuFreqLabel->setStyleSheet("font-size: 10px; color: #95a5a6;");
    gpuInfoLayout->addWidget(m_gpuFreqLabel);
    gpuLayout->addLayout(gpuInfoLayout);
    
    QWidget* gpuWidget = new QWidget();
    gpuWidget->setLayout(gpuLayout);
    grid->addWidget(gpuWidget, row + 1, 0, 1, 2);
    
    // ============================================================
    // 7. VPU (行row, 列2-3)
    // ============================================================
    QLabel* vpuTitle = new QLabel("VPU 会话");
    vpuTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
    grid->addWidget(vpuTitle, row, 2, 1, 2);
    
    QVBoxLayout* vpuLayout = new QVBoxLayout();
    vpuLayout->setSpacing(4);
    
    m_vpuDecLabel = new QLabel("解码: 0 路");
    m_vpuDecLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2ecc71;");
    vpuLayout->addWidget(m_vpuDecLabel);
    
    m_vpuEncLabel = new QLabel("编码: 0 路");
    m_vpuEncLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
    vpuLayout->addWidget(m_vpuEncLabel);
    
    QWidget* vpuWidget = new QWidget();
    vpuWidget->setLayout(vpuLayout);
    vpuWidget->setStyleSheet("background-color: rgba(52, 73, 94, 0.5); border-radius: 4px; padding: 6px;");
    grid->addWidget(vpuWidget, row + 1, 2, 1, 2);
    
    mainLayout->addLayout(grid);
    
    // ---- 定时器 ----
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SystemDashboard::updateStats);
    m_timer->start(2000);
    updateStats();
}

SystemDashboard::~SystemDashboard()
{
    if (m_timer) m_timer->stop();
}

// ============================================================
// 读取文件（普通权限）
// ============================================================
QString SystemDashboard::readFile(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QTextStream stream(&file);
    QString content = stream.readAll().trimmed();
    file.close();
    return content;
}

// ============================================================
// 读取文件（sudo 权限，通过 QProcess）
// ============================================================
QString SystemDashboard::readFileWithSudo(const QString& path)
{
    QProcess process;
    process.start("/usr/bin/sudo", QStringList() << "cat" << path);
    if (!process.waitForFinished(500)) {
        return QString();
    }
    
    // 检查是否有错误输出
    QString error = process.readAllStandardError().trimmed();
    if (!error.isEmpty()) {
        qDebug() << "[readFileWithSudo] error:" << error;
        return QString();
    }
    
    QString output = QString(process.readAllStandardOutput()).trimmed();
    return output;
}

// ============================================================
// 1. CPU 更新 - 读取 /proc/stat 计算差值
// ============================================================
void SystemDashboard::updateCpu()
{
    QVector<QVector<long long>> current;
    
    QString content = readFile("/proc/stat");
    if (content.isEmpty()) {
        for (int i = 0; i < 8; i++) {
            m_cpuBars[i]->setValue(0);
            m_cpuLabels[i]->setText("N/A");
        }
        return;
    }
    
    QStringList lines = content.split('\n');
    int idx = 0;
    for (const QString& line : lines) {
        if (line.startsWith("cpu") && line[3] >= '0' && line[3] <= '9' && idx < 8) {
            QStringList parts = line.split(' ', QString::SkipEmptyParts);
            if (parts.size() >= 5) {
                QVector<long long> stats(4);
                stats[0] = parts[1].toLongLong();  // user
                stats[1] = parts[2].toLongLong();  // nice
                stats[2] = parts[3].toLongLong();  // system
                stats[3] = parts[4].toLongLong();  // idle
                current.append(stats);
                idx++;
            }
        }
    }
    
    if (current.size() < 8) return;
    
    // 第一次读取，只保存数据不计算
    if (m_firstCpuRead) {
        for (int i = 0; i < 8; i++) {
            m_prevUser[i] = current[i][0];
            m_prevNice[i] = current[i][1];
            m_prevSystem[i] = current[i][2];
            m_prevIdle[i] = current[i][3];
        }
        m_firstCpuRead = false;
        return;
    }
    
    for (int i = 0; i < 8; i++) {
        long long user = current[i][0] - m_prevUser[i];
        long long nice = current[i][1] - m_prevNice[i];
        long long system = current[i][2] - m_prevSystem[i];
        long long idle = current[i][3] - m_prevIdle[i];
        long long total = user + nice + system + idle;
        
        int usage = (total > 0) ? (int)((user + nice + system) * 100 / total) : 0;
        
        m_cpuBars[i]->setValue(usage);
        m_cpuLabels[i]->setText(QString("%1%").arg(usage));
        
        QString color = (usage > 80) ? "#e74c3c" : (usage > 50) ? "#f39c12" : "#3498db";
        m_cpuBars[i]->setStyleSheet(
            QString("QProgressBar { background-color: #34495e; border-radius: 2px; height: 10px; }"
                    "QProgressBar::chunk { border-radius: 2px; background-color: %1; }").arg(color)
        );
        
        // 保存当前值供下次计算
        m_prevUser[i] = current[i][0];
        m_prevNice[i] = current[i][1];
        m_prevSystem[i] = current[i][2];
        m_prevIdle[i] = current[i][3];
    }
}

// ============================================================
// 2. 内存更新 - 读取 /proc/meminfo
// ============================================================
void SystemDashboard::updateMemory()
{
    QString totalStr = readFile("/proc/meminfo");
    if (totalStr.isEmpty()) {
        m_memoryBar->setValue(0);
        m_memoryLabel->setText("N/A");
        return;
    }
    
    QStringList lines = totalStr.split('\n');
    int totalKB = 0, availKB = 0;
    for (const QString& line : lines) {
        if (line.startsWith("MemTotal:")) {
            totalKB = line.split(' ', QString::SkipEmptyParts)[1].toInt();
        } else if (line.startsWith("MemAvailable:")) {
            availKB = line.split(' ', QString::SkipEmptyParts)[1].toInt();
        }
    }
    
    if (totalKB > 0 && availKB > 0) {
        int usedKB = totalKB - availKB;
        int totalMB = totalKB / 1024;
        int usedMB = usedKB / 1024;
        int percent = usedKB * 100 / totalKB;
        
        m_memoryBar->setValue(percent);
        m_memoryLabel->setText(QString("%1%").arg(percent));
        m_memoryDetailLabel->setText(QString("已用: %1 MB / 总计: %2 MB").arg(usedMB).arg(totalMB));
        
        QString color = (percent > 90) ? "#e74c3c" : (percent > 70) ? "#f39c12" : "#2ecc71";
        m_memoryBar->setStyleSheet(
            QString("QProgressBar { background-color: #34495e; border-radius: 3px; height: 14px; }"
                    "QProgressBar::chunk { border-radius: 3px; background-color: %1; }").arg(color)
        );
    }
}

// ============================================================
// 3. NPU 更新 (需要 sudo)
//    输出格式: "NPU load:  Core0: 54%, Core1: 51%, Core2: 55%,"
//    使用正则表达式提取每个核心的负载
// ============================================================
void SystemDashboard::updateNpu()
{
    QString output = readFileWithSudo("/sys/kernel/debug/rknpu/load");
    
    // 存储三个核心的负载，-1 表示未读取到
    int load0 = -1, load1 = -1, load2 = -1;
    
    if (!output.isEmpty()) {
        // 正则匹配 "Core0: 54%" 格式
        QRegExp rx("Core(\\d+):\\s*(\\d+)%");
        int pos = 0;
        while ((pos = rx.indexIn(output, pos)) != -1) {
            int coreIdx = rx.cap(1).toInt();
            int load = rx.cap(2).toInt();
            if (coreIdx == 0) load0 = load;
            else if (coreIdx == 1) load1 = load;
            else if (coreIdx == 2) load2 = load;
            pos += rx.matchedLength();
        }
    }
    
    // 更新三个NPU核心显示
    QVector<int> loads = {load0, load1, load2};
    for (int i = 0; i < 3; i++) {
        if (loads[i] >= 0) {
            m_npuBars[i]->setValue(loads[i]);
            m_npuLabels[i]->setText(QString("%1%").arg(loads[i]));
            QString color = (loads[i] > 80) ? "#e74c3c" : (loads[i] > 50) ? "#f39c12" : "#9b59b6";
            m_npuBars[i]->setStyleSheet(
                QString("QProgressBar { background-color: #34495e; border-radius: 2px; height: 10px; }"
                        "QProgressBar::chunk { border-radius: 2px; background-color: %1; }").arg(color)
            );
        } else {
            m_npuBars[i]->setValue(0);
            m_npuLabels[i]->setText("N/A");
        }
    }
}

// ============================================================
// 4. RGA 更新 (需要 sudo)
//    输出格式:
//    scheduler[0]: rga3    load = 11%
//    scheduler[1]: rga3    load = 4%
//    scheduler[2]: rga2    load = 4%
//    使用正则表达式按顺序提取三个核心的负载
// ============================================================
void SystemDashboard::updateRga()
{
    QString output = readFileWithSudo("/sys/kernel/debug/rkrga/load");
    
    int load0 = -1, load1 = -1, load2 = -1;
    
    if (!output.isEmpty()) {
        // 匹配 "load = 11%" 格式，按顺序提取
        QRegExp rx("load\\s*=\\s*(\\d+)%");
        int pos = 0;
        int idx = 0;
        while ((pos = rx.indexIn(output, pos)) != -1 && idx < 3) {
            int load = rx.cap(1).toInt();
            if (idx == 0) load0 = load;
            else if (idx == 1) load1 = load;
            else if (idx == 2) load2 = load;
            idx++;
            pos += rx.matchedLength();
        }
    }
    
    // 更新三个RGA核心显示
    QVector<int> loads = {load0, load1, load2};
    for (int i = 0; i < 3; i++) {
        if (loads[i] >= 0) {
            m_rgaBars[i]->setValue(loads[i]);
            m_rgaLabels[i]->setText(QString("%1%").arg(loads[i]));
            
            // ★ 微调：低负载时进度条颜色不变，值小但进度条仍然可见（最小值显示2%）
            // 进度条值最小显示2%，让低负载也能看到一点
            int displayValue = (loads[i] < 2) ? 2 : loads[i];
            m_rgaBars[i]->setValue(displayValue);
        } else {
            m_rgaBars[i]->setValue(0);
            m_rgaLabels[i]->setText("N/A");
        }
    }
}
// ============================================================
// 5. DDR 更新 - 读取频率和负载
// ============================================================
void SystemDashboard::updateDdr()
{
    // 读取频率
    QString freqStr = readFile("/sys/class/devfreq/dmc/cur_freq");
    if (!freqStr.isEmpty()) {
        int freq = freqStr.toInt();
        m_ddrFreqLabel->setText(QString("频率: %1 MHz").arg(freq / 1000000));
    } else {
        m_ddrFreqLabel->setText("频率: N/A");
    }
    
    // 读取负载
    QString loadStr = readFile("/sys/class/devfreq/dmc/load");
    if (!loadStr.isEmpty()) {
        // 格式: "30@2112000000Hz"，提取 @ 前面的数字
        int atPos = loadStr.indexOf('@');
        QString percentStr = (atPos > 0) ? loadStr.left(atPos) : loadStr;
        bool ok;
        int load = percentStr.toInt(&ok);
        if (ok) {
            m_ddrBar->setValue(load);
            m_ddrLoadLabel->setText(QString("负载: %1%").arg(load));
        } else {
            m_ddrBar->setValue(0);
            m_ddrLoadLabel->setText("负载: N/A");
        }
    } else {
        m_ddrBar->setValue(0);
        m_ddrLoadLabel->setText("负载: N/A");
    }
}

// ============================================================
// 6. GPU 更新 - 读取负载和频率
// ============================================================
void SystemDashboard::updateGpu()
{
    QString gpuPath = "/sys/devices/platform/fb000000.gpu/devfreq/fb000000.gpu/load";
    QString output = readFile(gpuPath);
    
    if (!output.isEmpty()) {
        int atPos = output.indexOf('@');
        if (atPos > 0) {
            // 提取负载
            QString percentStr = output.left(atPos);
            bool ok;
            int load = percentStr.toInt(&ok);
            if (ok) {
                m_gpuBar->setValue(load);
                m_gpuLoadLabel->setText(QString("负载: %1%").arg(load));
                QString color = (load > 80) ? "#e74c3c" : (load > 50) ? "#f39c12" : "#e74c3c";
                m_gpuBar->setStyleSheet(
                    QString("QProgressBar { background-color: #34495e; border-radius: 3px; height: 14px; }"
                            "QProgressBar::chunk { border-radius: 3px; background-color: %1; }").arg(color)
                );
            }
            
            // 提取频率
            QString freqStr = output.mid(atPos + 1);
            if (freqStr.endsWith("Hz")) {
                freqStr = freqStr.left(freqStr.length() - 2);
                bool freqOk;
                int freq = freqStr.toLongLong(&freqOk);
                if (freqOk) {
                    m_gpuFreqLabel->setText(QString("频率: %1 MHz").arg(freq / 1000000));
                }
            }
        }
    } else {
        m_gpuBar->setValue(0);
        m_gpuLoadLabel->setText("负载: N/A");
        m_gpuFreqLabel->setText("频率: N/A");
    }
}

// ============================================================
// 7. VPU 更新 - 统计解码/编码会话数
// ============================================================
void SystemDashboard::updateVpu()
{
    QString output = readFile("/proc/mpp_service/sessions-summary");
    if (!output.isEmpty()) {
        int decCount = 0, encCount = 0;
        QStringList lines = output.split('\n');
        for (const QString& line : lines) {
            if (line.contains("device:")) {
                if (line.contains("rkvdec")) decCount++;
                else if (line.contains("rkvenc")) encCount++;
            }
        }
        m_vpuDecLabel->setText(QString("解码: %1 路").arg(decCount));
        m_vpuEncLabel->setText(QString("编码: %1 路").arg(encCount));
    } else {
        m_vpuDecLabel->setText("解码: 0 路");
        m_vpuEncLabel->setText("编码: 0 路");
    }
}

// ============================================================
// 8. 温度更新 - 读取 thermal zone
// ============================================================
void SystemDashboard::updateTemp()
{
    QString output = readFile("/sys/class/thermal/thermal_zone0/temp");
    if (!output.isEmpty()) {
        int temp = output.toInt() / 1000;
        m_tempLabel->setText(QString("%1°C").arg(temp));
        
        QString color = (temp > 80) ? "#e74c3c" : (temp > 60) ? "#f39c12" : "#2ecc71";
        m_tempLabel->setStyleSheet(
            QString("font-size: 18px; font-weight: bold; color: %1; "
                    "background-color: rgba(52, 73, 94, 0.5); padding: 4px 14px; border-radius: 6px;").arg(color)
        );
    } else {
        m_tempLabel->setText("N/A");
    }
}

// ============================================================
// 定时更新所有统计信息
// ============================================================
void SystemDashboard::updateStats()
{
    updateCpu();       // 1. CPU 8个核心
    updateMemory();    // 2. 内存使用情况
    updateNpu();       // 3. NPU 3个核心
    updateRga();       // 4. RGA 3个核心
    updateDdr();       // 5. DDR 带宽
    updateGpu();       // 6. GPU 使用率
    updateVpu();       // 7. VPU 会话
    updateTemp();      // 8. CPU 温度
}