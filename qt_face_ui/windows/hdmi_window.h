#ifndef HDMI_WINDOW_H
#define HDMI_WINDOW_H

#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QVector>
#include <QList>
#include <QMutex>
#include "app/constants.h"
#include "widgets/gl_stream_view.h"

//注意：这里没有信号，只有槽（其他模块调用这些 public 函数来更新显示）。
class HDMIMainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HDMIMainWindow(QWidget *parent = nullptr);
    ~HDMIMainWindow();

    void switchToGridMode();                    // 切换到 8 路网格模式
    void switchToFullscreenMode(int stream_id);  // 切换到单路全屏
    void setShowBoxes(bool show);               // 开关检测框
    void setShowRois(bool show);                // 开关 ROI 区域显示
    void updateFrame(int stream_id, int fd, int width, int height, int size);
    void updateBoxes(int stream_id, const QVector<DetectionBox>& boxes);
    // 更新某路设备的占用状态（主进程随帧下发），下发到对应 view
    void updateDeviceStatus(int stream_id, const QVector<int>& deviceIds, const QVector<bool>& occupied);

    // 读取 roi.conf 并下发到各路 view（启动时 + ROI 保存后调用）
    void reloadRoiConfig();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void clearLayout();          // 清除当前布局
    void createViews();          // 创建 8 个显示控件
    void applyRoisToViews();     // 把当前 ROI 列表下发到所有 view

    QVector<GLStreamView*>  m_views;        // 8 个 OpenGL 显示控件
    QGridLayout*            m_gridLayout;   // 网格布局
    QWidget*                m_fullscreenWidget;  // 全屏模式的容器
    int                     m_currentMode;       // 0=网格, 1=全屏
    int                     m_fullscreenStreamId; // 全屏时显示哪一路
    bool                    m_showBoxes;         // 是否显示检测框
    bool                    m_showRois;          // 是否显示 ROI 区域
    QList<RoiRect>          m_rois;              // 从 roi.conf 读到的全部 ROI
    QMutex                  m_mutex;             // 线程锁
};

#endif