#include "yuyv_texture.h"
#include <QDebug>

YUYVTexture::YUYVTexture()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_yTexture(nullptr)
    , m_uvTexture(nullptr)
{
}

YUYVTexture::~YUYVTexture()
{
    if (m_yTexture) delete m_yTexture;
    if (m_uvTexture) delete m_uvTexture;
}

void YUYVTexture::initialize(int width, int height)
{
    if (m_initialized) return;
    
    initializeOpenGLFunctions();
    
    m_width = width;
    m_height = height;
    
    // 纹理 A: Y 分量（GL_RG，存储两个像素的 Y 值）
    m_yTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_yTexture->setSize(width / 2, height);
    m_yTexture->setFormat(QOpenGLTexture::RG8_UNorm);
    m_yTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_yTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_yTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_yTexture->allocateStorage();
    
    // 纹理 B: UV 分量（GL_RGBA，每 2 个像素共享一对 UV）
    m_uvTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_uvTexture->setSize(width / 2, height);
    m_uvTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_uvTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_uvTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_uvTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_uvTexture->allocateStorage();
    
    m_initialized = true;
    qDebug() << "[YUYVTexture] Initialized:" << width << "x" << height;
}

bool YUYVTexture::updateFromDmaBuf(int fd, int size)
{
    if (!m_initialized) {
        qWarning() << "[YUYVTexture] Not initialized";
        return false;
    }
    
    if (fd < 0) {
        qWarning() << "[YUYVTexture] Invalid fd:" << fd;
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 映射 DMA-BUF
    void* mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        qWarning() << "[YUYVTexture] mmap failed";
        return false;
    }
    
    uint8_t* yuyv = (uint8_t*)mapped;
    int yuvSize = m_width * m_height * 2;  // YUYV 每像素 2 字节
    
    // 分配缓冲区
    int yTexSize = (m_width / 2) * m_height * 2;  // RG 格式，每像素 2 字节
    int uvTexSize = (m_width / 2) * m_height * 4; // RGBA 格式，每像素 4 字节
    
    uint8_t* yData = new uint8_t[yTexSize];
    uint8_t* uvData = new uint8_t[uvTexSize];
    
    // 解析 YUYV 数据到两个纹理
    for (int i = 0; i < yuvSize; i += 4) {
        int idx = i / 4;
        
        // Y 纹理：每 2 个像素的 Y 值
        yData[idx * 2] = yuyv[i];      // Y0
        yData[idx * 2 + 1] = yuyv[i + 2];  // Y1
        
        // UV 纹理：每 2 个像素共享的 UV
        uvData[idx * 4] = yuyv[i + 1];      // U
        uvData[idx * 4 + 1] = yuyv[i + 3];  // V
        uvData[idx * 4 + 2] = 0;
        uvData[idx * 4 + 3] = 0;
    }
    
    // 上传 Y 数据
    m_yTexture->bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    m_width / 2, m_height, GL_RG, GL_UNSIGNED_BYTE, yData);
    
    // 上传 UV 数据
    m_uvTexture->bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    m_width / 2, m_height, GL_RGBA, GL_UNSIGNED_BYTE, uvData);
    
    delete[] yData;
    delete[] uvData;
    munmap(mapped, size);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "[YUYVTexture] OpenGL error:" << err;
        return false;
    }
    
    return true;
}

void YUYVTexture::bind(int yUnit, int uvUnit)
{
    if (!m_initialized) return;
    
    glActiveTexture(GL_TEXTURE0 + yUnit);
    m_yTexture->bind();
    
    glActiveTexture(GL_TEXTURE0 + uvUnit);
    m_uvTexture->bind();
}