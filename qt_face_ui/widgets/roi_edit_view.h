#ifndef ROI_EDIT_VIEW_H
#define ROI_EDIT_VIEW_H

#include "gl_stream_view.h"
#include "app/constants.h"
#include <QPoint>
#include <QList>

/**
 * @brief ROI 编辑控件（继承 GLStreamView）
 *
 * 在视频画面之上叠加显示：
 *   - 当前流的所有设备 ROI（不同颜色区分）
 *   - 当前正在编辑的设备 ROI 高亮
 *   - 鼠标拖拽时的橡皮筋矩形
 *
 * 交互：
 *   - 鼠标左键按下 + 拖动 + 松开 = 框选一个矩形
 *   - 松开后通过 roiDrawn 信号上报（画面像素坐标）
 *   - 撤销由外部（RoiConfigWindow）调用 undo() 触发
 *
 * 坐标系：信号上报的是画面原始像素坐标（与 roi.conf / 检测框一致），
 *         内部按 widget 尺寸做线性换算（视频被拉伸铺满控件）。
 */
class RoiEditView : public GLStreamView
{
    Q_OBJECT

public:
    explicit RoiEditView(int streamId, QWidget* parent = nullptr);

    /**
     * @brief 设置当前流的所有 ROI（用于叠加显示）
     * @param rois 当前流的全部设备 ROI（画面坐标）
     */
    void setRois(const QList<RoiRect>& rois);

    /**
     * @brief 设置当前正在编辑的设备号（用于高亮）
     */
    void setCurrentDevice(int deviceId);

    /**
     * @brief 撤销最近一次框选（由外部触发，实际数据回滚由外部完成后再 setRois）
     *        这里仅刷新显示
     */
    void undo() { update(); }

signals:
    /**
     * @brief 鼠标框选完成信号
     * @param streamId 当前流
     * @param deviceId 当前设备
     * @param x,y,w,h  画面像素坐标
     */
    void roiDrawn(int streamId, int deviceId, int x, int y, int w, int h);

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawRoiOverlay(QPainter& painter);

    QList<RoiRect> m_rois;          // 当前流的全部 ROI
    int            m_currentDevice; // 当前编辑设备号
    bool           m_drawing;       // 是否正在拖拽
    QPoint         m_startPos;      // 拖拽起点（widget 坐标）
    QPoint         m_endPos;        // 拖拽终点（widget 坐标）
};

#endif // ROI_EDIT_VIEW_H
