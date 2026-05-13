#include "gl_stream_view.h"
#include <QDebug>
#include <QOpenGLContext>
#include <unistd.h>

static const float vertices[] = {
    -1.0f, -1.0f,           0.0f, 1.0f,
     1.0f, -1.0f,           1.0f, 1.0f,
    -1.0f,  1.0f,           0.0f, 0.0f,
     1.0f,  1.0f,           1.0f, 0.0f
};

GLStreamView::GLStreamView(int streamId, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_streamId(streamId)
    , m_frameWidth(0)
    , m_frameHeight(0)
    , m_frameFd(-1)
    , m_frameSize(0)
    , m_frameFormat(0)
    , m_hasFrame(false)
    , m_showBoxes(true)
    , m_showRois(false)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

GLStreamView::~GLStreamView()
{
    makeCurrent();
    m_vbo.destroy();
    doneCurrent();
    
    if (m_frameFd >= 0) {
        ::close(m_frameFd);
        m_frameFd = -1;
    }
}

void GLStreamView::updateFrame(int fd, int width, int height, int size, int format)
{
    if (fd < 0) return;
    
    int oldFd = -1;
    {
        QMutexLocker locker(&m_mutex);
        oldFd = m_frameFd;
        m_frameFd = dup(fd);
        m_frameWidth = width;
        m_frameHeight = height;
        m_frameSize = size;
        m_frameFormat = format;
        m_hasFrame = true;
    }
    
    if (oldFd >= 0) {
        ::close(oldFd);
    }
    
    update();
}

void GLStreamView::setDetectionBoxes(const QVector<DetectionBox>& boxes)
{
    QMutexLocker locker(&m_boxesMutex);
    m_boxes = boxes;
    update();
}

void GLStreamView::setShowBoxes(bool show)
{
    m_showBoxes = show;
    update();
}

void GLStreamView::setRoiBoxes(const QVector<DetectionBox>& rois, const QVector<int>& deviceIds)
{
    QMutexLocker locker(&m_roisMutex);
    m_rois = rois;
    m_roiDeviceIds = deviceIds;
    // 默认全部空闲（绿色），等待 setRoiDeviceStatus 更新
    m_roiOccupied.fill(false, rois.size());
    update();
}

void GLStreamView::setRoiDeviceStatus(const QVector<int>& deviceIds, const QVector<bool>& occupied)
{
    QMutexLocker locker(&m_roisMutex);
    // 按 device_id 匹配，更新对应 ROI 的占用状态
    for (int i = 0; i < m_roiDeviceIds.size(); i++) {
        int myId = m_roiDeviceIds[i];
        for (int j = 0; j < deviceIds.size(); j++) {
            if (deviceIds[j] == myId) {
                m_roiOccupied[i] = occupied[j];
                break;
            }
        }
    }
    update();
}

void GLStreamView::setShowRois(bool show)
{
    m_showRois = show;
    update();
}

void GLStreamView::clearFrame()
{
    int oldFd = -1;
    {
        QMutexLocker locker(&m_mutex);
        oldFd = m_frameFd;
        m_frameFd = -1;
        m_hasFrame = false;
    }
    
    if (oldFd >= 0) {
        ::close(oldFd);
    }
    update();
}

void GLStreamView::initializeGL()
{
    initializeOpenGLFunctions();
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // 初始化 NV12 Shader
    if (!m_nv12Shader.init(0)) {
        qWarning() << "[GLStreamView] Failed to init NV12 shader";
    }
    
    // 初始化 YUYV Shader
    if (!m_yuyvShader.init(1)) {
        qWarning() << "[GLStreamView] Failed to init YUYV shader";
    }
    
    initVertexBuffer();
    
    qDebug() << "[GLStreamView] Stream" << m_streamId << "initialized";
}

void GLStreamView::initVertexBuffer()
{
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));
    m_vbo.release();
}

void GLStreamView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GLStreamView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    bool hasFrame = false;
    int fd = -1, width = 0, height = 0, size = 0, format = 0;
    
    {
        QMutexLocker locker(&m_mutex);
        if (m_hasFrame && m_frameFd >= 0) {
            hasFrame = true;
            fd = dup(m_frameFd);
            width = m_frameWidth;
            height = m_frameHeight;
            size = m_frameSize;
            format = m_frameFormat;
        }
    }
    
    if (!hasFrame) {
        if (m_streamId == -1) {
            return;  // USB 摄像头黑屏
        }
        QPainter painter(this);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, 
                         QString("Stream %1\nWaiting...").arg(m_streamId + 1));
        return;
    }
    
    // ============================================================
    // 根据格式选择纹理和 Shader
    // ============================================================
    if (format == 0) {
        // NV12 格式（主进程监控画面）
        if (!m_nv12Texture.isInitialized() || 
            m_nv12Texture.width() != width || 
            m_nv12Texture.height() != height) {
            m_nv12Texture.initialize(width, height);
        }
        
        if (!m_nv12Texture.updateFromDmaBuf(fd, size)) {
            qWarning() << "[GLStreamView] Failed to update NV12 texture";
            ::close(fd);
            return;
        }
        
        // 绑定 NV12 纹理并使用 NV12 Shader
        m_nv12Texture.bind(0, 1);
        m_nv12Shader.bindNV12(0, 1);
        
    } else if (format == 1) {
        // YUYV 格式（USB 摄像头）
        if (!m_yuyvTexture.isInitialized() || 
            m_yuyvTexture.width() != width || 
            m_yuyvTexture.height() != height) {
            m_yuyvTexture.initialize(width, height);
        }
        
        if (!m_yuyvTexture.updateFromDmaBuf(fd, size)) {
            qWarning() << "[GLStreamView] Failed to update YUYV texture";
            ::close(fd);
            return;
        }
        
        // 绑定双纹理
        m_yuyvTexture.bind(0, 1);
        m_yuyvShader.bindYUYV(0, 1);
    }
    
    // 获取当前激活的 Shader
    YUVShader* activeShader = (format == 0) ? &m_nv12Shader : &m_yuyvShader;
    int posAttr = activeShader->posAttr();
    int texCoordAttr = activeShader->texCoordAttr();
    
    // 渲染四边形
    m_vbo.bind();
    activeShader->enableAttributeArray(posAttr);
    activeShader->enableAttributeArray(texCoordAttr);
    
    int stride = 4 * sizeof(float);
    activeShader->setAttributeBuffer(posAttr, GL_FLOAT, 0, 2, stride);
    activeShader->setAttributeBuffer(texCoordAttr, GL_FLOAT, 2 * sizeof(float), 2, stride);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    activeShader->disableAttributeArray(posAttr);
    activeShader->disableAttributeArray(texCoordAttr);
    m_vbo.release();
    activeShader->release();
    
    ::close(fd);

    // 绘制检测框（监控画面显示，USB 不显示）
    if (m_showBoxes && m_streamId != -1) {
        QPainter painter(this);
        drawDetectionBoxes(painter);
    }

    // 绘制 ROI 区域（绿色框，仅监控画面）
    if (m_showRois && m_streamId != -1) {
        QPainter painter(this);
        drawRoiBoxes(painter);
    }
}

void GLStreamView::drawDetectionBoxes(QPainter& painter)
{
    QVector<DetectionBox> boxes;
    {
        QMutexLocker locker(&m_boxesMutex);
        boxes = m_boxes;
    }

    if (boxes.isEmpty()) return;

    painter.setRenderHint(QPainter::Antialiasing);

    int viewW = width();
    int viewH = height();

    int srcW = m_frameWidth;
    int srcH = m_frameHeight;

    if (srcW <= 0 || srcH <= 0) return;

    QPen pen(Qt::red);
    pen.setWidth(3);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    for (const DetectionBox& box : boxes) {
        int dispX = box.x * viewW / srcW;
        int dispY = box.y * viewH / srcH;
        int dispW = box.w * viewW / srcW;
        int dispH = box.h * viewH / srcH;

        dispX = qMax(0, qMin(dispX, viewW - 1));
        dispY = qMax(0, qMin(dispY, viewH - 1));
        dispW = qMin(dispW, viewW);
        dispH = qMin(dispH, viewH);

        if (dispW > 0 && dispH > 0) {
            painter.drawRect(dispX, dispY, dispW, dispH);
        }
    }
}

void GLStreamView::drawRoiBoxes(QPainter& painter)
{
    QVector<DetectionBox> rois;
    QVector<bool> occupied;
    {
        QMutexLocker locker(&m_roisMutex);
        rois     = m_rois;
        occupied = m_roiOccupied;
    }

    if (rois.isEmpty()) return;

    painter.setRenderHint(QPainter::Antialiasing);

    int viewW = width();
    int viewH = height();

    int srcW = m_frameWidth;
    int srcH = m_frameHeight;

    if (srcW <= 0 || srcH <= 0) return;

    painter.setBrush(Qt::NoBrush);

    // 文字字体
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(qMax(8, viewW / 60));
    painter.setFont(font);

    for (int i = 0; i < rois.size(); i++) {
        const DetectionBox& roi = rois[i];
        bool isOccupied = (i < occupied.size()) ? occupied[i] : false;

        // 空闲=绿色，使用中=红色
        QColor boxColor = isOccupied ? QColor(231, 76, 60)   // 红
                                     : QColor(46, 204, 113); // 绿
        QPen pen(boxColor);
        pen.setWidth(3);
        painter.setPen(pen);

        int dispX = roi.x * viewW / srcW;
        int dispY = roi.y * viewH / srcH;
        int dispW = roi.w * viewW / srcW;
        int dispH = roi.h * viewH / srcH;

        dispX = qMax(0, qMin(dispX, viewW - 1));
        dispY = qMax(0, qMin(dispY, viewH - 1));
        dispW = qMin(dispW, viewW);
        dispH = qMin(dispH, viewH);

        if (dispW > 0 && dispH > 0) {
            painter.drawRect(dispX, dispY, dispW, dispH);

            // 左上角状态标签：使用中 / 空闲
            QString label = isOccupied ? QStringLiteral("使用中") : QStringLiteral("空闲");
            int tagH = font.pointSize() + 8;
            int tagW = painter.fontMetrics().horizontalAdvance(label) + 12;
            // 标签背景半透明，颜色与框一致
            painter.fillRect(dispX, dispY, tagW, tagH, QColor(boxColor.red(), boxColor.green(), boxColor.blue(), 180));
            painter.setPen(Qt::white);
            painter.drawText(QRect(dispX + 6, dispY + 2, tagW - 12, tagH - 4), Qt::AlignVCenter | Qt::AlignLeft, label);
            painter.setPen(boxColor);  // 恢复，下一轮画框
        }
    }
}