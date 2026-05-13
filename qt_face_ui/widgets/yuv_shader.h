#ifndef YUV_SHADER_H
#define YUV_SHADER_H

#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>

/**
 * @brief 通用 YUV→RGB 转换 Shader
 * 
 * 支持多种 YUV 格式：
 * - NV12: Y 平面 + UV 交错平面
 * - YUYV: YUYV 交错格式（通过纹理采样实现）
 * 
 * 顶点着色器：传递位置和纹理坐标
 * 片段着色器：根据输入格式做不同的 YUV→RGB 转换
 */
class YUVShader : public QOpenGLShaderProgram
{
public:
    YUVShader();
    ~YUVShader();

    /**
     * @brief 初始化并编译 Shader
     * @param yuvType 0=NV12, 1=YUYV
     * @return true 成功, false 失败
     */
    bool init(int yuvType = 0);

    /**
     * @brief 绑定 NV12 纹理（Y 和 UV 分开）
     * @param yUnit   Y 平面的纹理单元索引
     * @param uvUnit  UV 平面的纹理单元索引
     */
    void bindNV12(int yUnit, int uvUnit);

    /**
     * @brief 绑定 YUYV 纹理（单一纹理）
     * @param unit 纹理单元索引
     */
    void bindYUYV(int unit, int width);

    /**
     * @brief 获取位置属性位置
     */
    int posAttr() const { return m_posAttr; }

    /**
     * @brief 获取纹理坐标属性位置
     */
    int texCoordAttr() const { return m_texCoordAttr; }

private:
    int m_posAttr;      // 顶点位置属性
    int m_texCoordAttr; // 纹理坐标属性
    int m_yUniform;     // Y 纹理 uniform (NV12)
    int m_uvUniform;    // UV 纹理 uniform (NV12)
    int m_yuyvUniform;  // YUYV 纹理 uniform
    int m_yuvType;      // YUV 类型: 0=NV12, 1=YUYV
};

#endif