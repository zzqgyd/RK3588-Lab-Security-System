#include "hdmi_window.h"
#include <QDebug>
#include <QResizeEvent>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <unistd.h>

HDMIMainWindow::HDMIMainWindow(QWidget *parent)
    : QWidget(parent)
    , m_gridLayout(nullptr)
    , m_fullscreenWidget(nullptr)
    , m_currentMode(0)
    , m_fullscreenStreamId(-1)
    , m_showBoxes(true)
    , m_showRois(false)
{
    setWindowTitle("HDMI Monitor - Face System");
    setStyleSheet("background-color: black;");
    setWindowFlags(Qt::FramelessWindowHint);

    // 创建8个显示控件
    createViews();

    // 默认网格模式
    switchToGridMode();

    // 启动时读取 ROI 配置并下发（默认不显示，由显示控制页开关）
    reloadRoiConfig();

    qDebug() << "[HDMIWindow] Created";
}

HDMIMainWindow::~HDMIMainWindow()
{
    // 清理所有控件
    for (auto* view : m_views) {
        if (view) {
            view->deleteLater();
        }
    }
    m_views.clear();
    
    if (m_fullscreenWidget) {
        m_fullscreenWidget->deleteLater();
    }
    
    qDebug() << "[HDMIWindow] Destroyed";
}

void HDMIMainWindow::createViews()
{
    m_views.clear();
    for (int i = 0; i < MAX_STREAMS; i++) {
        // 因为等下要把它们放进布局，布局会自动设置父对象。
        // 如果现在设置了父对象，后面再放进布局时可能会出问题。
        GLStreamView* view = new GLStreamView(i, nullptr);
        view->setShowBoxes(m_showBoxes);
        view->setShowRois(m_showRois);
        m_views.append(view);
    }
    applyRoisToViews();
}

void HDMIMainWindow::switchToGridMode()
{
    if (m_currentMode == 0 && m_gridLayout) return;// 已经是网格模式，不用切换
    
    // 清除全屏模式
    if (m_fullscreenWidget) {
        //delete 是立即释放内存，但如果此时 Qt 事件循环正在处理这个 widget 的事件
        // ，就会崩溃。deleteLater() 把删除推迟到事件循环空闲时，安全。
        m_fullscreenWidget->deleteLater();// 标记删除，事件循环时真正删除
        m_fullscreenWidget = nullptr;
    }
    
    // 清除当前布局
    clearLayout();
    
    // 创建网格布局
    m_gridLayout = new QGridLayout(this);
    m_gridLayout->setSpacing(2);              // 摄像头画面之间间隔2像素
    m_gridLayout->setContentsMargins(0, 0, 0, 0);  // 无边距，铺满
    
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int idx = row * GRID_COLS + col;
            if (idx < MAX_STREAMS) {
                // 重新创建控件（避免 OpenGL 上下文问题）
                // OpenGL 上下文是和父窗口绑定的。当从全屏切回网格时，
                // 控件被移除了，OpenGL 上下文可能已经失效。
                // 重新创建可以保证干净的 OpenGL 环境。，避免纹理丢失、黑屏。
                if (m_views[idx]) {
                    m_views[idx]->deleteLater();
                }
                m_views[idx] = new GLStreamView(idx, this);
                m_views[idx]->setShowBoxes(m_showBoxes);
                m_views[idx]->setShowRois(m_showRois);
                m_gridLayout->addWidget(m_views[idx], row, col);
            }
        }
    }
    
    setLayout(m_gridLayout);
    m_currentMode = 0;
    m_fullscreenStreamId = -1;

    applyRoisToViews();

    qDebug() << "[HDMIWindow] Switched to grid mode";
}

void HDMIMainWindow::switchToFullscreenMode(int stream_id)
{
    // 边界检查
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return;
    // 防重复切换
    if (m_currentMode == 1 && m_fullscreenStreamId == stream_id) return;
    
    // 清除当前布局
    clearLayout();
    
    // 重新创建全屏控件（避免 OpenGL 上下文问题）
    if (m_views[stream_id]) {
        m_views[stream_id]->deleteLater();
    }
    
    GLStreamView* fullscreenView = new GLStreamView(stream_id, nullptr);
    fullscreenView->setShowBoxes(m_showBoxes);
    fullscreenView->setShowRois(m_showRois);
    m_views[stream_id] = fullscreenView;
    // 给新建的全屏 view 下发该路 ROI（含 device_id）
    {
        QVector<DetectionBox> r;
        QVector<int> ids;
        for (const RoiRect& rr : m_rois) {
            if (rr.streamId == stream_id) {
                DetectionBox b; b.x = rr.x; b.y = rr.y; b.w = rr.w; b.h = rr.h;
                r.append(b);
                ids.append(rr.deviceId);
            }
        }
        fullscreenView->setRoiBoxes(r, ids);
    }
    
    /**创建全屏容器
     * HDMIMainWindow
            └── QVBoxLayout (mainLayout)
                └── m_fullscreenWidget
                    └── QVBoxLayout
                        └── GLStreamView (stream_id)  ← 铺满 1920×1080
     */
    m_fullscreenWidget = new QWidget(this);            // ① 创建一个空容器
    QVBoxLayout* layout = new QVBoxLayout(m_fullscreenWidget); // ② 给容器装一个垂直布局
    layout->setContentsMargins(0, 0, 0, 0);            // ③ 布局无边距
    layout->addWidget(fullscreenView);                 // ④ 把OpenGL控件塞进去
    m_fullscreenWidget->setLayout(layout);             // ⑤ 确认布局生效
    
    // 设置为中央控件
    QVBoxLayout* mainLayout = new QVBoxLayout(this);  // ⑥ 给整个窗口装布局
    mainLayout->setContentsMargins(0, 0, 0, 0);       // ⑦ 无边距
    mainLayout->addWidget(m_fullscreenWidget);         // ⑧ 把刚才那个容器放进去
    setLayout(mainLayout);                             // ⑨ 让窗口用这个布局
    
    m_currentMode = 1;
    m_fullscreenStreamId = stream_id;
    
    qDebug() << "[HDMIWindow] Switched to fullscreen mode, stream:" << stream_id;
}

// 由 DSI 屏的复选框触发，
// 通过 FaceApplication 转发到这里，统一控制所有8路画面的检测框显示。
void HDMIMainWindow::setShowBoxes(bool show)
{
    m_showBoxes = show;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (m_views[i]) {
            m_views[i]->setShowBoxes(show);
        }
    }
}

// 控制是否在 HDMI 大屏上显示各路 ROI 区域（绿色框）
void HDMIMainWindow::setShowRois(bool show)
{
    m_showRois = show;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (m_views[i]) {
            m_views[i]->setShowRois(show);
        }
    }
}

// 读取 roi.conf（与主进程同一文件），按路下发到对应 view
void HDMIMainWindow::reloadRoiConfig()
{
    QList<RoiRect> rois;
    QFile f(ROI_CONFIG_PATH);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
            if (r.deviceId < 1) continue;
            rois.append(r);
        }
        f.close();
    }
    m_rois = rois;
    qDebug() << "[HDMIWindow] ROI 重新加载:" << m_rois.size() << "条";
    applyRoisToViews();
}

// 把 m_rois 按路下发到所有 view（同时下发 device_id）
void HDMIMainWindow::applyRoisToViews()
{
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!m_views[i]) continue;
        QVector<DetectionBox> r;
        QVector<int> ids;
        for (const RoiRect& rr : m_rois) {
            if (rr.streamId == i) {
                DetectionBox b; b.x = rr.x; b.y = rr.y; b.w = rr.w; b.h = rr.h;
                r.append(b);
                ids.append(rr.deviceId);
            }
        }
        m_views[i]->setRoiBoxes(r, ids);
    }
}

void HDMIMainWindow::updateFrame(int stream_id, int fd, int width, int height, int size)
{
    // 边界检查
    if (stream_id < 0 || stream_id >= MAX_STREAMS) {
        if (fd >= 0) ::close(fd);  // 非法ID，关掉fd防止泄漏
        return;
    }
    
    // 更新对应显示控件（主进程画面都是 NV12 格式，format=0）
    if (m_views[stream_id]) {
        m_views[stream_id]->updateFrame(fd, width, height, size, 0);
    } 

    // 无论有没有 view，主进程传来的 fd 在这里关
    if (fd >= 0) ::close(fd);
}

void HDMIMainWindow::updateBoxes(int stream_id, const QVector<DetectionBox>& boxes)
{
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return;
    if (m_views[stream_id]) {
        m_views[stream_id]->setDetectionBoxes(boxes);
    }
}

void HDMIMainWindow::clearLayout()
{
    //返回当前窗口正在使用的布局对象。
    QLayout* oldLayout = layout();
    if (oldLayout) {
        // 清空布局但不删除子控件（子控件会被重新创建）
        // takeAt() 把子项从布局里移除，但不销毁 widget 本身。然后销毁布局对象。
        while (QLayoutItem* item = oldLayout->takeAt(0)) {
            // 不删除 widget
            // 因为切换模式时会主动 deleteLater() 旧的 GLStreamView，
            // 如果这里也 delete，会重复删除导致崩溃。
        }
        delete oldLayout;
    }
}

void HDMIMainWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

// 主进程随帧下发的设备占用状态，下发到对应路的 view
void HDMIMainWindow::updateDeviceStatus(int stream_id, const QVector<int>& deviceIds, const QVector<bool>& occupied)
{
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return;
    if (m_views[stream_id]) {
        m_views[stream_id]->setRoiDeviceStatus(deviceIds, occupied);
    }
}