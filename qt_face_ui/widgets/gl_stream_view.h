#ifndef GL_STREAM_VIEW_H
#define GL_STREAM_VIEW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QMutex>
#include <QVector>
#include <QPainter>
#include "nv12_texture.h"
#include "yuyv_texture.h"
#include "yuv_shader.h"
#include "app/constants.h"

/**
 * @brief OpenGL 视频显示控件
 * 
 * 职责：
 * - 显示单路视频流
 * - 支持 NV12 格式（主进程监控画面）
 * - 支持 YUYV 格式（USB 摄像头画面）
 * - 统一使用 YUVShader 做 GPU 颜色空间转换
 */
class GLStreamView : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit GLStreamView(int streamId, QWidget* parent = nullptr);
    ~GLStreamView();

    void updateFrame(int fd, int width, int height, int size, int format);
    void setDetectionBoxes(const QVector<DetectionBox>& boxes);
    void setShowBoxes(bool show);
    void clearFrame();
    int streamId() const { return m_streamId; }
    void setStreamId(int id) { m_streamId = id; }

    // ===== ROI 区域显示（按设备状态着色，用于 HDMI 大屏）=====
    // rois 与 deviceIds 一一对应
    void setRoiBoxes(const QVector<DetectionBox>& rois, const QVector<int>& deviceIds);
    void setShowRois(bool show);
    // 按设备编号更新占用状态（true=使用中/红, false=空闲/绿）
    void setRoiDeviceStatus(const QVector<int>& deviceIds, const QVector<bool>& occupied);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // 子类（RoiEditView）可访问的画面尺寸，用于坐标换算
    int frameWidth()  const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }
    bool hasFrame()   const { return m_hasFrame; }

private:
    void initVertexBuffer();
    void drawDetectionBoxes(QPainter& painter);
    void drawRoiBoxes(QPainter& painter);

    int  m_streamId;
    int  m_frameWidth;
    int  m_frameHeight;
    int  m_frameFd;
    int  m_frameSize;
    int  m_frameFormat;          // 0=NV12, 1=YUYV
    bool m_hasFrame;
    bool m_showBoxes;
    bool m_showRois;
    QMutex m_mutex;

    NV12Texture     m_nv12Texture;   // NV12 纹理
    YUYVTexture     m_yuyvTexture;   // YUYV 纹理
    YUVShader       m_nv12Shader;    // NV12 YUV 转换 Shader
    YUVShader       m_yuyvShader;    // YUYV YUV 转换 Shader

    QOpenGLBuffer   m_vbo;
    QVector<DetectionBox> m_boxes;
    QMutex          m_boxesMutex;

    // ROI 区域：rois 与 roiDeviceIds 一一对应
    QVector<DetectionBox> m_rois;          // ROI 矩形（画面坐标）
    QVector<int>          m_roiDeviceIds;  // 每个 ROI 对应的设备编号
    QVector<bool>         m_roiOccupied;   // 每个 ROI 是否使用中
    QMutex                m_roisMutex;
};

#endif