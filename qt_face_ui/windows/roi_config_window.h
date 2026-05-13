#ifndef ROI_CONFIG_WINDOW_H
#define ROI_CONFIG_WINDOW_H

#include <QWidget>
#include <QList>
#include <QMap>
#include <QStack>
#include <QPair>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include "app/constants.h"   // RoiRect / MAX_STREAMS / ROI_MAX_DEVICES

class RoiEditView;

/**
 * @brief ROI 区域配置窗口
 *
 * 工作流：
 *   1. 打开时读取 roi.conf，默认显示第 0 路流 + 该路最前面的设备 ROI
 *   2. 在画面上鼠标框选 -> 更新当前 (流,设备) 的 ROI（支持撤销）
 *   3. 切换设备 / 切换流继续框选
 *   4. 点击保存：仅覆盖/新增被框选过的 (流,设备) 条目，其它条目原样保留
 *   5. 保存后发出 roiSaved 信号（由 FaceApplication 通知主进程热重载）
 *
 * 合并写入规则（满足"只改动的才覆盖"）：
 *   - 以 (streamId, deviceId) 为粒度
 *   - 被框选过的 -> 用新矩形覆盖
 *   - 未框选的 -> 保留原文件内容
 *   - 新增的 -> 追加
 */
class RoiConfigWindow : public QWidget
{
    Q_OBJECT

public:
    explicit RoiConfigWindow(QWidget *parent = nullptr);
    ~RoiConfigWindow();

    /**
     * @brief 接收主进程画面帧（由 FaceApplication 转发）
     *        仅接受当前编辑流的帧
     */
    void updateFrame(int stream_id, int fd, int width, int height, int size, int format);

signals:
    /**
     * @brief ROI 配置已保存信号
     *        FaceApplication 收到后通过 socket 通知主进程热重载
     */
    void roiSaved();

    /**
     * @brief 返回主页
     */
    void backToHome();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onStreamChanged(int index);
    void onDeviceChanged(int index);
    void onRoiDrawn(int streamId, int deviceId, int x, int y, int w, int h);
    void onUndo();
    void onNextDevice();
    void onNextStream();
    void onSave();
    void onBack();

private:
    void loadConfigFile();
    void rebuildDeviceCombo();
    void refreshRoiDisplay();
    QList<RoiRect> effectiveRoisForStream(int streamId) const;
    int nextStreamId() const;

    // UI
    RoiEditView*  m_view;
    QComboBox*    m_streamCombo;
    QComboBox*    m_deviceCombo;
    QPushButton*  m_btnUndo;
    QPushButton*  m_btnNextDevice;
    QPushButton*  m_btnNextStream;
    QPushButton*  m_btnSave;
    QPushButton*  m_btnBack;
    QLabel*       m_infoLabel;

    // 数据
    QList<RoiRect> m_original;   // 从 roi.conf 读到的全部条目
    // 编辑表：key=(streamId,deviceId) -> 修改后的 ROI（坐标/宽高有效）
    QMap<QPair<int,int>, RoiRect> m_edits;
    // 撤销栈：记录被框选的 (streamId,deviceId)，撤销时回滚该条目
    QStack<QPair<int,int>> m_undoStack;

    int m_currentStream;
    int m_currentDevice;
    bool m_loaded;
};

#endif // ROI_CONFIG_WINDOW_H
