#include "ShaderResource.h"
#include "YuvShaderResource.h"
#include <QDebug>
#ifdef _WIN32
#include "D3D11ShaderResource.h"
#endif

namespace view {
void ShaderResource::updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_vsVertexTransformMatrix) {
        qCritical() << "vertex transform matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_vsVertexTransformMatrix.get(), 0, sizeof(float) * 4 * 4, matrix.constData());
}

void ShaderResource::updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_fsColorRangeConversionMatrix) {
        qCritical() << "color range conversion matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_fsColorRangeConversionMatrix.get(), 0, sizeof(float) * 4 * 4, matrix.constData());
}

void ShaderResource::updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!rub || !m_fsYUVtoRGBMatrix) {
        qCritical() << "YUV to RGB matrix buffer is not ready";
        return;
    }
    rub->updateDynamicBuffer(m_fsYUVtoRGBMatrix.get(), 0, sizeof(float) * 4 * 4, matrix.constData());
}

ShaderResource::ShaderResource(const Params& params)
    : m_type(params.type), m_rhi(params.rhi), m_size(params.size), m_swPixelFormat(params.swPixelFormat)
{
}

bool ShaderResource::initialize()
{
    if (m_rhi == nullptr) {
        qCritical() << "called with null QRhi";
        return false;
    }

    // 公共 buffer 必须先于派生类资源创建：派生类在 initializeResources()
    // 里建 SRB 时会直接引用它们
    if (!createMatrixBuffers()) {
        return false;
    }

    return initializeResources();
}

bool ShaderResource::createMatrixBuffers()
{
    QRhi* rhi = m_rhi;
    if (rhi == nullptr) {
        qCritical() << "called before QRhi is set";
        return false;
    }

    m_vsVertexTransformMatrix.reset(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float) * 4 * 4));
    if (!m_vsVertexTransformMatrix->create()) {
        qCritical() << "Failed to create vertex uniform buffer";
        return false;
    }
    m_fsColorRangeConversionMatrix.reset(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float) * 4 * 4));
    if (!m_fsColorRangeConversionMatrix->create()) {
        qCritical() << "Failed to create fragment color buffer";
        return false;
    }
    m_fsYUVtoRGBMatrix.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float) * 4 * 4));
    if (!m_fsYUVtoRGBMatrix->create()) {
        qCritical() << "Failed to create fragment YUV range buffer";
        return false;
    }

    return true;
}
std::unique_ptr<ShaderResource> ShaderResource::create(const Params& params)
{
    switch (params.type) {
        case Type::Yuv: {
            std::unique_ptr<ShaderResource> res = std::make_unique<YuvShaderResource>(params);
            if (res->initialize()) {
                return res;
            }
        } break;
#ifdef _WIN32
        case Type::D3D11: {
            std::unique_ptr<ShaderResource> res = std::make_unique<D3D11ShaderResource>(params);
            if (res->initialize()) {
                return res;
            }
        } break;
#endif
        default: qWarning() << "Unsupported shader resource type."; break;
    }
    return nullptr;
}
} // namespace view
