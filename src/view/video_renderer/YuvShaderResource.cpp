#include "YuvShaderResource.h"
#include <QDebug>

namespace view {
void YuvShaderResource::updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    rub->updateDynamicBuffer(m_vsVertexTransformMatrix.get(), 0, sizeof(float)*4*4, &matrix);
}

void YuvShaderResource::updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    rub->updateDynamicBuffer(m_fsColorRangeConversionMatrix.get(), 0, sizeof(float)*4*4, &matrix);
}

void YuvShaderResource::updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    rub->updateDynamicBuffer(m_fsYUVtoRGBMatrix.get(), 0, sizeof(float)*4*4, &matrix);
}

void YuvShaderResource::updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame)
{
    if (!frame) {
        qCritical() << "Null frame passed to updateTexture";
        return;
    }

    auto uploadTextureData = [&](QRhiTexture* texture, const uint8_t* data, int width, int height, int lineSize) {
        if (!data) {
            qCritical() << "Null data passed to uploadTextureData";
            return;
        }

        QRhiTextureSubresourceUploadDescription sub(data, lineSize*height);
        sub.setSourceSize(QSize(width, height));
        sub.setDataStride(lineSize);
        QRhiTextureUploadEntry entry{0, 0, sub};
        QRhiTextureUploadDescription desc{{entry}};
        rub->uploadTexture(texture, desc);
    };

    uploadTextureData(m_yTexture.get(), frame->yData(), frame->width(), frame->height(), frame->yLineSize());
    uploadTextureData(m_uTexture.get(), frame->uData(), frame->width() / 2, frame->height() / 2, frame->uLineSize());
    uploadTextureData(m_vTexture.get(), frame->vData(), frame->width() / 2, frame->height() / 2, frame->vLineSize());
}

bool YuvShaderResource::initialize(const Params& params)
{
    if (params.type != Type::Yuv) {
        qCritical() << "called with incorrect type";
        return false;
    }

    if (params.rhi == nullptr) {
        qCritical() << "called with null QRhi";
        return false;
    }
    QRhi* rhi = params.rhi;

    // 初始化纹理
    QSize size = params.size;
    QSize uvSize(size.width() / 2, size.height() / 2);
    m_yTexture.reset(rhi->newTexture(QRhiTexture::R8, size, 1, QRhiTexture::Flags{}));
    m_uTexture.reset(rhi->newTexture(QRhiTexture::R8, uvSize, 1, QRhiTexture::Flags{}));
    m_vTexture.reset(rhi->newTexture(QRhiTexture::R8, uvSize, 1, QRhiTexture::Flags{}));
    if (!m_yTexture->create() || !m_uTexture->create() || !m_vTexture->create()) {
        qCritical() << "failed to create YUV textures";
        return false;
    }

    m_sampler.reset(rhi->newSampler(
        QRhiSampler::Linear,        // 放大过滤，指像素放大时如何插值
        QRhiSampler::Linear,        // 缩小过滤，指像素缩小时如何插值
        QRhiSampler::None,          // mipmap过滤，禁用了mipmap所以使用None
        QRhiSampler::ClampToEdge,   // U方向(水平方向)的纹理坐标超出[0,1]范围时的处理方式
        QRhiSampler::ClampToEdge    // V方向(垂直方向)的纹理坐标超出[0,1]范围时的处理方式
    ));
    if (!m_sampler->create()) {
        qCritical() << "Failed to create sampler";
        return false;
    }

    // uniform buffers 初始化
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

    // 创建资源绑定
    m_srb.reset(rhi->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_vTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
        QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())
    });
    if (!m_srb->create()) {
        qCritical() << "Failed to create shader resource bindings";
        return false;
    }

    return true;
}

} // namespace view