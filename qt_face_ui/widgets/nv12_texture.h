#ifndef NV12_TEXTURE_H
#define NV12_TEXTURE_H

#include <QOpenGLTexture>
#include <QOpenGLFunctions>
#include <QMutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/**
 * @brief NV12 纹理管理类
 * 
 * 职责：
 * - 管理两个 OpenGL 纹理（Y 平面和 UV 平面）
 * - 从 DMA-BUF fd 更新纹理数据
 * - 零拷贝：通过 mmap 直接访问 DMA-BUF 内存
 * 
 * 纹理格式：
 * - Y 平面: R8_UNorm (单通道)
 * - UV 平面: RG8_UNorm (双通道交错)
 */
class NV12Texture : protected QOpenGLFunctions
{
public:
    NV12Texture();
    ~NV12Texture();

    /**
     * @brief 初始化纹理（在 OpenGL 上下文中调用）
     * @param width  图像宽度
     * @param height 图像高度
     */
    void initialize(int width, int height);

    /**
     * @brief 从 DMA-BUF fd 更新纹理数据
     * @param fd    DMA-BUF 文件描述符
     * @param size  数据大小
     * @return true 成功, false 失败
     */
    bool updateFromDmaBuf(int fd, int size);

    /**
     * @brief 绑定纹理到 OpenGL 单元
     * @param yUnit  Y 平面的纹理单元 (GL_TEXTURE0 + yUnit)
     * @param uvUnit UV 平面的纹理单元 (GL_TEXTURE0 + uvUnit)
     */
    void bind(int yUnit = 0, int uvUnit = 1);

    /**
     * @brief 获取图像宽度
     */
    int width() const { return m_width; }

    /**
     * @brief 获取图像高度
     */
    int height() const { return m_height; }

    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized;
    int  m_width;
    int  m_height;
    
    QOpenGLTexture* m_textureY;   // Y 平面纹理 (亮度)
    QOpenGLTexture* m_textureUV;  // UV 平面纹理 (色度)
    QMutex          m_mutex;      // 保护纹理更新
};

#endif