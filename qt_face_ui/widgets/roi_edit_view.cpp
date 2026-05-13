#include "roi_edit_view.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPen>
#include <QFont>

RoiEditView::RoiEditView(int streamId, QWidget* parent)
    : GLStreamView(streamId, parent)
    , m_currentDevice(1)
    , m_drawing(false)
{
    // 编辑视图默认不显示检测框，避免和 ROI 叠加混乱
    setShowBoxes(false);
    setShowRois(false);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void RoiEditView::setRois(const QList<RoiRect>& rois)
{
    m_rois = rois;
    update();
}

void RoiEditView::setCurrentDevice(int deviceId)
{
    m_currentDevice = deviceId;
    update();
}

void RoiEditView::paintGL()
{
    // 先让基类渲染视频画面（含检测框/ROI 开关由基类自行处理）
    GLStreamView::paintGL();

    // 再叠加 ROI 编辑层
    QPainter painter(this);
    drawRoiOverlay(painter);
}

void RoiEditView::drawRoiOverlay(QPainter& painter)
{
    int srcW = frameWidth();
    int srcH = frameHeight();
    int viewW = width();
    int viewH = height();

    // 没有画面时给出提示
    if (!hasFrame() || srcW <= 0 || srcH <= 0) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter,
                         QString("流 %1\n等待画面...\n鼠标框选设备 ROI 区域").arg(streamId() + 1));
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    // 1. 绘制当前流的全部设备 ROI
    for (const RoiRect& r : m_rois) {
        int dx = r.x * viewW / srcW;
        int dy = r.y * viewH / srcH;
        int dw = r.w * viewW / srcW;
        int dh = r.h * viewH / srcH;

        QPen pen;
        if (r.deviceId == m_currentDevice) {
            pen.setColor(QColor(255, 165, 0));   // 橙色：当前编辑设备
            pen.setWidth(4);
        } else {
            pen.setColor(QColor(0, 200, 255));   // 青色：其它设备
            pen.setWidth(3);
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(dx, dy, dw, dh);

        // 标注设备号
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPointSize(12);
        painter.setFont(f);
        painter.drawText(dx + 4, dy + 16, QString("设备%1").arg(r.deviceId));
    }

    // 2. 绘制橡皮筋（拖拽中）
    if (m_drawing) {
        QPen pen(QColor(255, 0, 255));   // 紫色：正在框选
        pen.setWidth(3);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 0, 255, 40));
        QRect rect = QRect(m_startPos, m_endPos).normalized();
        painter.drawRect(rect);
    }
}

void RoiEditView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (!hasFrame()) return;   // 没画面不允许框选
    m_drawing = true;
    m_startPos = event->pos();
    m_endPos = event->pos();
    update();
}

void RoiEditView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_drawing) return;
    m_endPos = event->pos();
    update();
}

void RoiEditView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_drawing) return;
    m_drawing = false;
    m_endPos = event->pos();

    int srcW = frameWidth();
    int srcH = frameHeight();
    int viewW = width();
    int viewH = height();
    if (srcW <= 0 || srcH <= 0 || viewW <= 0 || viewH <= 0) {
        update();
        return;
    }

    QRect r = QRect(m_startPos, m_endPos).normalized();
    // 太小的框忽略
    if (r.width() < 4 || r.height() < 4) {
        update();
        return;
    }

    // widget 坐标 -> 画面像素坐标（线性拉伸映射）
    int fx = r.x() * srcW / viewW;
    int fy = r.y() * srcH / viewH;
    int fw = r.width() * srcW / viewW;
    int fh = r.height() * srcH / viewH;

    // 边界裁剪
    fx = qMax(0, qMin(fx, srcW - 1));
    fy = qMax(0, qMin(fy, srcH - 1));
    fw = qMin(fw, srcW - fx);
    fh = qMin(fh, srcH - fy);

    if (fw > 0 && fh > 0) {
        emit roiDrawn(streamId(), m_currentDevice, fx, fy, fw, fh);
    }
    update();
}
