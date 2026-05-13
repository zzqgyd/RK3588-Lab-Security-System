#include "roi_config_window.h"
#include "widgets/roi_edit_view.h"
#include "app/constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMessageBox>
#include <QRegularExpression>
#include <algorithm>
#include <unistd.h>

RoiConfigWindow::RoiConfigWindow(QWidget *parent)
    : QWidget(parent)
    , m_view(nullptr)
    , m_streamCombo(nullptr)
    , m_deviceCombo(nullptr)
    , m_btnUndo(nullptr)
    , m_btnNextDevice(nullptr)
    , m_btnNextStream(nullptr)
    , m_btnSave(nullptr)
    , m_btnBack(nullptr)
    , m_infoLabel(nullptr)
    , m_currentStream(0)
    , m_currentDevice(1)
    , m_loaded(false)
{
    setWindowTitle("ROI 区域配置");
    setWindowFlags(Qt::FramelessWindowHint);
    setStyleSheet("background-color: #2c3e50;");

    // ============================================================
    // 左侧：视频 + 框选
    // ============================================================
    m_view = new RoiEditView(m_currentStream, this);
    m_view->setMinimumSize(640, 360);
    m_view->setStyleSheet("background-color: black;");

    // ============================================================
    // 右侧：控制面板
    // ============================================================
    QWidget* panel = new QWidget(this);
    panel->setFixedWidth(280);
    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setSpacing(12);
    panelLayout->setContentsMargins(10, 10, 10, 10);

    QLabel* title = new QLabel("ROI 区域配置");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #3498db;");
    panelLayout->addWidget(title);

    // 流选择
    QHBoxLayout* sLayout = new QHBoxLayout();
    QLabel* sLabel = new QLabel("流:");
    sLabel->setStyleSheet("color: white; font-size: 16px;");
    sLabel->setFixedWidth(50);
    m_streamCombo = new QComboBox();
    for (int i = 0; i < MAX_STREAMS; i++) {
        m_streamCombo->addItem(QString("流 %1").arg(i));
    }
    m_streamCombo->setStyleSheet("font-size: 16px; padding: 6px;");
    sLayout->addWidget(sLabel);
    sLayout->addWidget(m_streamCombo);
    panelLayout->addLayout(sLayout);

    // 设备选择
    QHBoxLayout* dLayout = new QHBoxLayout();
    QLabel* dLabel = new QLabel("设备:");
    dLabel->setStyleSheet("color: white; font-size: 16px;");
    dLabel->setFixedWidth(50);
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setStyleSheet("font-size: 16px; padding: 6px;");
    dLayout->addWidget(dLabel);
    dLayout->addWidget(m_deviceCombo);
    panelLayout->addLayout(dLayout);

    // 信息标签
    m_infoLabel = new QLabel("");
    m_infoLabel->setStyleSheet("color: #ecf0f1; font-size: 14px; background-color: #34495e; padding: 8px; border-radius: 6px;");
    m_infoLabel->setWordWrap(true);
    panelLayout->addWidget(m_infoLabel);

    // 按钮
    QString btnStyle = "QPushButton { background-color: #34495e; color: white; font-size: 15px; border-radius: 8px; padding: 10px; }"
                       "QPushButton:pressed { background-color: #1a252f; }";
    QString accentStyle = "QPushButton { background-color: #27ae60; color: white; font-size: 16px; border-radius: 8px; padding: 12px; font-weight: bold; }"
                          "QPushButton:pressed { background-color: #229954; }";

    m_btnUndo = new QPushButton("撤销 (Undo)");
    m_btnNextDevice = new QPushButton("下一个 ROI 区域");
    m_btnNextStream = new QPushButton("下一路流");
    m_btnSave = new QPushButton("保存配置");
    m_btnBack = new QPushButton("返回主页");

    m_btnUndo->setStyleSheet(btnStyle);
    m_btnNextDevice->setStyleSheet(btnStyle);
    m_btnNextStream->setStyleSheet(btnStyle);
    m_btnSave->setStyleSheet(accentStyle);
    m_btnBack->setStyleSheet("QPushButton { background-color: #7f8c8d; color: white; font-size: 15px; border-radius: 8px; padding: 10px; }"
                             "QPushButton:pressed { background-color: #6c7a7a; }");

    panelLayout->addWidget(m_btnUndo);
    panelLayout->addWidget(m_btnNextDevice);
    panelLayout->addWidget(m_btnNextStream);
    panelLayout->addStretch();
    panelLayout->addWidget(m_btnSave);
    panelLayout->addWidget(m_btnBack);

    // ============================================================
    // 主布局
    // ============================================================
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_view, 1);
    mainLayout->addWidget(panel);

    // ============================================================
    // 信号连接
    // ============================================================
    connect(m_streamCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onStreamChanged(int)));
    connect(m_deviceCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onDeviceChanged(int)));
    connect(m_view, &RoiEditView::roiDrawn, this, &RoiConfigWindow::onRoiDrawn);
    connect(m_btnUndo, &QPushButton::clicked, this, &RoiConfigWindow::onUndo);
    connect(m_btnNextDevice, &QPushButton::clicked, this, &RoiConfigWindow::onNextDevice);
    connect(m_btnNextStream, &QPushButton::clicked, this, &RoiConfigWindow::onNextStream);
    connect(m_btnSave, &QPushButton::clicked, this, &RoiConfigWindow::onSave);
    connect(m_btnBack, &QPushButton::clicked, this, &RoiConfigWindow::onBack);

    // 提示文字
    m_infoLabel->setText("操作说明:\n1. 鼠标拖拽框选设备区域\n2. 可撤销重选\n3. 切换设备/流继续\n4. 全部完成后点保存");
}

RoiConfigWindow::~RoiConfigWindow()
{
}

void RoiConfigWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_loaded) {
        loadConfigFile();
        m_loaded = true;
    }
    // 默认第 0 路流 + 该路最前面的设备 ROI
    m_currentStream = 0;
    m_view->setStreamId(m_currentStream);
    m_streamCombo->blockSignals(true);
    m_streamCombo->setCurrentIndex(0);
    m_streamCombo->blockSignals(false);
    rebuildDeviceCombo();
}

void RoiConfigWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
}

// ================================================================
// 读取 roi.conf 到 m_original（按行解析，不做默认填充）
// ================================================================
void RoiConfigWindow::loadConfigFile()
{
    m_original.clear();
    QFile f(ROI_CONFIG_PATH);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[ROI] 配置文件不存在，按空配置开始:" << ROI_CONFIG_PATH;
        return;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 6) continue;
        RoiRect r;
        r.streamId = parts[0].toInt();
        r.deviceId = parts[1].toInt();
        r.x = parts[2].toInt();
        r.y = parts[3].toInt();
        r.w = parts[4].toInt();
        r.h = parts[5].toInt();
        if (r.streamId < 0 || r.streamId >= MAX_STREAMS) continue;
        if (r.deviceId < 1 || r.deviceId > ROI_MAX_DEVICES) continue;
        m_original.append(r);
    }
    f.close();
    qDebug() << "[ROI] 读入" << m_original.size() << "条配置";
}

// ================================================================
// 重建设备下拉框（当前流的全部设备 1..ROI_MAX_DEVICES）
// 默认选中该流最前面已配置的设备，否则设备1
// ================================================================
void RoiConfigWindow::rebuildDeviceCombo()
{
    // 找到当前流已配置的最小设备号
    int firstDev = -1;
    for (const RoiRect& r : m_original) {
        if (r.streamId == m_currentStream) {
            if (firstDev < 0 || r.deviceId < firstDev) firstDev = r.deviceId;
        }
    }
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it) {
        if (it.key().first == m_currentStream) {
            if (firstDev < 0 || it.key().second < firstDev) firstDev = it.key().second;
        }
    }
    if (firstDev < 0) firstDev = 1;

    // 暂时阻塞信号，避免重建时触发 onDeviceChanged
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    for (int d = 1; d <= ROI_MAX_DEVICES; d++) {
        m_deviceCombo->addItem(QString("设备 %1").arg(d), d);
    }
    int idx = m_deviceCombo->findData(firstDev);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
    m_deviceCombo->blockSignals(false);

    m_currentDevice = firstDev;
    m_view->setCurrentDevice(m_currentDevice);
    refreshRoiDisplay();
}

// ================================================================
// 当前流的有效 ROI 列表（原始 + 编辑合并）
// ================================================================
QList<RoiRect> RoiConfigWindow::effectiveRoisForStream(int streamId) const
{
    QList<RoiRect> result;
    // 先放编辑过的
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it) {
        if (it.key().first == streamId) result.append(it.value());
    }
    // 再放未编辑的原始条目
    for (const RoiRect& r : m_original) {
        if (r.streamId != streamId) continue;
        QPair<int,int> key(r.streamId, r.deviceId);
        if (!m_edits.contains(key)) result.append(r);
    }
    return result;
}

void RoiConfigWindow::refreshRoiDisplay()
{
    m_view->setRois(effectiveRoisForStream(m_currentStream));

    // 更新信息标签：显示当前设备 ROI 坐标
    QPair<int,int> key(m_currentStream, m_currentDevice);
    QString coords;
    if (m_edits.contains(key)) {
        RoiRect r = m_edits.value(key);
        coords = QString("已框选: x=%1 y=%2 w=%3 h=%4").arg(r.x).arg(r.y).arg(r.w).arg(r.h);
    } else {
        bool found = false;
        for (const RoiRect& r : m_original) {
            if (r.streamId == m_currentStream && r.deviceId == m_currentDevice) {
                coords = QString("原配置: x=%1 y=%2 w=%3 h=%4").arg(r.x).arg(r.y).arg(r.w).arg(r.h);
                found = true;
                break;
            }
        }
        if (!found) coords = "未配置（请框选）";
    }
    m_infoLabel->setText(QString("流 %1 / 设备 %2\n%3\n\n操作:\n· 鼠标拖拽框选\n· 撤销重选\n· 切换后继续\n· 完成后保存")
                                 .arg(m_currentStream).arg(m_currentDevice).arg(coords));
}

// ================================================================
// 槽
// ================================================================
void RoiConfigWindow::onStreamChanged(int index)
{
    if (index < 0 || index >= MAX_STREAMS) return;
    m_currentStream = index;
    m_view->setStreamId(m_currentStream);
    rebuildDeviceCombo();
    // rebuildDeviceCombo 内部已 refreshRoiDisplay
}

void RoiConfigWindow::onDeviceChanged(int index)
{
    if (index < 0) return;
    int dev = m_deviceCombo->itemData(index).toInt();
    if (dev < 1) return;
    m_currentDevice = dev;
    m_view->setCurrentDevice(m_currentDevice);
    refreshRoiDisplay();
}

void RoiConfigWindow::onRoiDrawn(int streamId, int deviceId, int x, int y, int w, int h)
{
    if (streamId != m_currentStream) return;
    QPair<int,int> key(streamId, deviceId);
    RoiRect r;
    r.streamId = streamId;
    r.deviceId = deviceId;
    r.x = x; r.y = y; r.w = w; r.h = h;
    m_edits[key] = r;
    m_undoStack.push(key);
    qDebug() << "[ROI] 框选 流" << streamId << "设备" << deviceId << ":" << x << y << w << h;
    refreshRoiDisplay();
}

void RoiConfigWindow::onUndo()
{
    if (m_undoStack.isEmpty()) {
        m_infoLabel->setText("没有可撤销的操作");
        return;
    }
    QPair<int,int> key = m_undoStack.pop();
    m_edits.remove(key);
    qDebug() << "[ROI] 撤销 流" << key.first << "设备" << key.second;
    // 切换到被撤销的设备位置，方便重选
    if (key.first != m_currentStream) {
        m_streamCombo->setCurrentIndex(key.first);
    }
    int idx = m_deviceCombo->findData(key.second);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
    refreshRoiDisplay();
}

void RoiConfigWindow::onNextDevice()
{
    int idx = m_deviceCombo->currentIndex();
    int next = (idx + 1) % m_deviceCombo->count();
    m_deviceCombo->setCurrentIndex(next);
}

int RoiConfigWindow::nextStreamId() const
{
    return (m_currentStream + 1) % MAX_STREAMS;
}

void RoiConfigWindow::onNextStream()
{
    m_streamCombo->setCurrentIndex(nextStreamId());
}

void RoiConfigWindow::onSave()
{
    // ============================================================
    // 合并写入：以 (streamId,deviceId) 为粒度
    //   - 编辑表覆盖原条目
    //   - 未编辑的原条目保留
    //   - 编辑表中的新条目追加
    // ============================================================

    // 先收集所有要写的条目，按 (stream,device) 去重，编辑优先
    QMap<QPair<int,int>, RoiRect> merged;
    for (const RoiRect& r : m_original) {
        merged.insert(QPair<int,int>(r.streamId, r.deviceId), r);
    }
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it) {
        merged.insert(it.key(), it.value());
    }

    if (merged.isEmpty()) {
        QMessageBox::warning(this, "提示", "没有任何 ROI 配置可保存");
        return;
    }

    QFile f(ROI_CONFIG_PATH);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", QString("无法写入配置文件:\n%1").arg(ROI_CONFIG_PATH));
        return;
    }
    QTextStream out(&f);
    // 按 stream 升序，再按 device 升序输出
    QList<QPair<int,int>> keys = merged.keys();
    std::sort(keys.begin(), keys.end());
    for (const QPair<int,int>& k : keys) {
        const RoiRect& r = merged.value(k);
        out << r.streamId << ' ' << r.deviceId << ' '
            << r.x << ' ' << r.y << ' ' << r.w << ' ' << r.h << '\n';
    }
    f.close();

    // 保存后：原文件就是新基线，清空编辑表/撤销栈
    m_original = merged.values();
    m_edits.clear();
    m_undoStack.clear();

    qDebug() << "[ROI] 配置已保存，共" << merged.size() << "条";
    QMessageBox::information(this, "成功", "ROI 配置已保存，主进程将自动重载");
    emit roiSaved();
}

void RoiConfigWindow::onBack()
{
    emit backToHome();
}

// ================================================================
// 帧转发：仅接受当前编辑流
// 注意：本函数不关闭 fd（fd 由 HDMI 窗口统一关闭），
//       GLStreamView::updateFrame 内部会 dup 使用。
// ================================================================
void RoiConfigWindow::updateFrame(int stream_id, int fd, int width, int height, int size, int format)
{
    if (stream_id != m_currentStream) {
        return;   // 非当前编辑流，不消费 fd
    }
    m_view->updateFrame(fd, width, height, size, format);
}
