#ifndef YUYV_TEXTURE_H
#define YUYV_TEXTURE_H

#include <QOpenGLTexture>
#include <QOpenGLFunctions>
#include <QMutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/**
 * @brief YUYV 纹理管理类（双纹理方案）
 * 
 * 纹理方案（解决 packed 格式采样问题）：
 * - m_yTexture: GL_RG 格式，存储 Y 分量（每像素 2 字节，包含两个像素的 Y 值）
 * - m_uvTexture: GL_RGBA 格式，存储 UV 分量（每 2 个像素共享一对 UV）
 * 
 * 零拷贝：通过 mmap 直接访问 DMA-BUF 内存
 */
class YUYVTexture : protected QOpenGLFunctions
{
public:
    YUYVTexture();
    ~YUYVTexture();

    void initialize(int width, int height);
    bool updateFromDmaBuf(int fd, int size);
    void bind(int yUnit = 0, int uvUnit = 1);
    bool isInitialized() const { return m_initialized; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    bool m_initialized;
    int  m_width;
    int  m_height;
    
    QOpenGLTexture* m_yTexture;   // Y 分量纹理（GL_RG）
    QOpenGLTexture* m_uvTexture;  // UV 分量纹理（GL_RGBA）
    QMutex          m_mutex;
};

#endif