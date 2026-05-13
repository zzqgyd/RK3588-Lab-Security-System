#include "yuv_shader.h"
#include <QDebug>

// 顶点着色器（通用）
static const char* vertexShaderSource = R"(
    attribute vec4 a_position;
    attribute vec2 a_texcoord;
    varying vec2 v_texcoord;
    void main() {
        gl_Position = a_position;
        v_texcoord = a_texcoord;
    }
)";

// NV12 片段着色器
static const char* fragmentShaderNV12 = R"(
    uniform sampler2D u_textureY;
    uniform sampler2D u_textureUV;
    varying vec2 v_texcoord;
    void main() {
        float y = texture2D(u_textureY, v_texcoord).r;
        vec2 uv = texture2D(u_textureUV, v_texcoord).rg;
        float u = uv.r - 0.5;
        float v = uv.g - 0.5;
        
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        
        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

// YUYV 片段着色器（已解包为 Y 和 UV 双纹理）
static const char* fragmentShaderYUYV = R"(
    uniform sampler2D u_textureY;
    uniform sampler2D u_textureUV;
    varying vec2 v_texcoord;
    void main() {
        // Y 纹理：GL_RG 格式，每个纹素包含两个像素的 Y 值
        vec2 yValues = texture2D(u_textureY, v_texcoord).rg;
        
        // UV 纹理：GL_RGBA 格式，每个纹素包含一对 UV 值
        vec2 uvValues = texture2D(u_textureUV, v_texcoord).rg;
        
        float y = yValues.r;
        float u = uvValues.r - 0.5;
        float v = uvValues.g - 0.5;
        
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        
        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

YUVShader::YUVShader()
    : m_posAttr(-1), m_texCoordAttr(-1), m_yUniform(-1), m_uvUniform(-1), m_yuvType(0)
{
}

YUVShader::~YUVShader()
{
}

bool YUVShader::init(int yuvType)
{
    m_yuvType = yuvType;
    
    if (!addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qWarning() << "[YUVShader] Failed to compile vertex shader";
        return false;
    }
    
    const char* fragmentSource = (yuvType == 0) ? fragmentShaderNV12 : fragmentShaderYUYV;
    if (!addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        qWarning() << "[YUVShader] Failed to compile fragment shader for type" << yuvType;
        return false;
    }
    
    if (!link()) {
        qWarning() << "[YUVShader] Failed to link shader program";
        return false;
    }
    
    m_posAttr = attributeLocation("a_position");
    m_texCoordAttr = attributeLocation("a_texcoord");
    m_yUniform = uniformLocation("u_textureY");
    m_uvUniform = uniformLocation("u_textureUV");
    
    qDebug() << "[YUVShader] Initialized for type" << (yuvType == 0 ? "NV12" : "YUYV");
    return true;
}

void YUVShader::bindNV12(int yUnit, int uvUnit)
{
    bind();
    if (m_yUniform >= 0) setUniformValue(m_yUniform, yUnit);
    if (m_uvUniform >= 0) setUniformValue(m_uvUniform, uvUnit);
}

void YUVShader::bindYUYV(int yUnit, int uvUnit)
{
    bind();
    if (m_yUniform >= 0) setUniformValue(m_yUniform, yUnit);
    if (m_uvUniform >= 0) setUniformValue(m_uvUniform, uvUnit);
}