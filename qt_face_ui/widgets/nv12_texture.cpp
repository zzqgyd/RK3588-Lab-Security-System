#include "nv12_texture.h"
#include <QDebug>
#include <QOpenGLContext>

NV12Texture::NV12Texture()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_textureY(nullptr)
    , m_textureUV(nullptr)
{
}

NV12Texture::~NV12Texture()
{
    if (m_textureY) {
        delete m_textureY;
        m_textureY = nullptr;
    }
    if (m_textureUV) {
        delete m_textureUV;
        m_textureUV = nullptr;
    }
}

void NV12Texture::initialize(int width, int height)
{
    if (m_initialized) return;
    
    initializeOpenGLFunctions();
    
    m_width = width;
    m_height = height;
    
    // 创建 Y 平面纹理（单通道，存储亮度）
    // 格式: R8_UNorm，每个像素1字节
    m_textureY = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_textureY->setSize(width, height);
    m_textureY->setFormat(QOpenGLTexture::R8_UNorm); // 单字节红色通道
    m_textureY->setMinificationFilter(QOpenGLTexture::Linear);
    m_textureY->setMagnificationFilter(QOpenGLTexture::Linear);
    m_textureY->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_textureY->allocateStorage();
    
    // 创建 UV 平面纹理（双通道交错，存储色度）
    // NV12 格式中 U 和 V 交错存储，所以使用 RG8_UNorm 格式
    m_textureUV = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_textureUV->setSize(width / 2, height / 2);// UV 平面是 Y 平面的 1/4
    m_textureUV->setFormat(QOpenGLTexture::RG8_UNorm);// 双字节 RG 通道
    m_textureUV->setMinificationFilter(QOpenGLTexture::Linear);
    m_textureUV->setMagnificationFilter(QOpenGLTexture::Linear);
    m_textureUV->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_textureUV->allocateStorage();
    
    m_initialized = true;
    qDebug() << "[NV12Texture] Initialized:" << width << "x" << height;
}

bool NV12Texture::updateFromDmaBuf(int fd, int size)
{
    if (!m_initialized) {
        qWarning() << "[NV12Texture] Not initialized";
        return false;
    }
    
    if (fd < 0) {
        qWarning() << "[NV12Texture] Invalid fd:" << fd;
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // ================================================================
    // 映射 DMA-BUF 到用户空间（零拷贝，只建立映射）
    // ================================================================
    void* mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        qWarning() << "[NV12Texture] mmap failed for fd:" << fd;
        return false;
    }
    
    uint8_t* y_plane = (uint8_t*)mapped;
    uint8_t* uv_plane = y_plane + m_width * m_height;
    
    // ================================================================
    // 上传 Y 平面数据到纹理
    // ================================================================
    m_textureY->bind();
    //只更新数据，不重新分配，效率高
    //把 CPU 内存中的图像数据上传到 GPU 纹理。只更新子区域（这里是整个纹理）
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 
                    m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, y_plane);
    
    // 检查 OpenGL 错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "[NV12Texture] OpenGL error after Y plane upload:" << err;
    }
    
    // ================================================================
    // 上传 UV 平面数据到纹理
    // NV12 中 UV 是交错存储的，格式为 GL_RG（双通道）
    // ================================================================
    m_textureUV->bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    m_width / 2, m_height / 2, GL_RG, GL_UNSIGNED_BYTE, uv_plane);
    
    err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "[NV12Texture] OpenGL error after UV plane upload:" << err;
    }
    
    // 解除映射
    munmap(mapped, size);
    
    return true;
}

void NV12Texture::bind(int yUnit, int uvUnit)
{
    if (!m_initialized) return;
    
    // 绑定 Y 纹理到指定纹理单元
    glActiveTexture(GL_TEXTURE0 + yUnit);
    m_textureY->bind();
    
    // 绑定 UV 纹理到指定纹理单元
    glActiveTexture(GL_TEXTURE0 + uvUnit);
    m_textureUV->bind();
}