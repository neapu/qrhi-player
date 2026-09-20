#include "ShaderResource.h"
#include "YuvShaderResource.h"
#include <QDebug>

namespace view {
void ShaderResource::updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_vsVertexTransformMatrix) {
        qCritical() << "vertex transform matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_vsVertexTransformMatrix.get(), 0, sizeof(float)*4*4, matrix.constData());
}

void ShaderResource::updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_fsColorRangeConversionMatrix) {
        qCritical() << "color range conversion matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_fsColorRangeConversionMatrix.get(), 0, sizeof(float)*4*4, matrix.constData());
}

void ShaderResource::updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_fsYUVtoRGBMatrix) {
        qCritical() << "YUV to RGB matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_fsYUVtoRGBMatrix.get(), 0, sizeof(float)*4*4, matrix.constData());
}

bool ShaderResource::initialize(const Params& params)
{
    if (params.rhi == nullptr) {
        qCritical() << "called with null QRhi";
        return false;
    }
    m_rhi = params.rhi;

    // 公共 buffer 必须先于派生类资源创建：派生类在 initializeResources()
    // 里建 SRB 时会直接引用它们
    if (!createMatrixBuffers()) {
        return false;
    }

    return initializeResources(params);
}

bool ShaderResource::createMatrixBuffers()
{
    QRhi* rhi = m_rhi;
    if (rhi == nullptr) {
        qCritical() << "called before QRhi is set";
        return false;
    }

    m_vsVertexTransformMatrix.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_vsVertexTransformMatrix->create()) {
        qCritical() << "Failed to create vertex uniform buffer";
        return false;
    }
    m_fsColorRangeConversionMatrix.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_fsColorRangeConversionMatrix->create()) {
        qCritical() << "Failed to create fragment color buffer";
        return false;
    }
    m_fsYUVtoRGBMatrix.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_fsYUVtoRGBMatrix->create()) {
        qCritical() << "Failed to create fragment YUV range buffer";
        return false;
    }

    return true;
}
std::unique_ptr<ShaderResource> ShaderResource::create(const Params& params)
{
    switch (params.type) {
    case Type::Yuv:
        {
            std::unique_ptr<ShaderResource> res = std::make_unique<YuvShaderResource>();
            if (res->initialize(params)) {
                return res;
            }
        }
        break;
    }
    return nullptr;
}
} // namespace view