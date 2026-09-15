#include "YuvShaderResource.h"

namespace view {
YuvShaderResource::YuvShaderResource(const QSize& size)
    : m_size(size)
{
}

bool YuvShaderResource::initialize(QRhi* rhi)
{
    if (!rhi) {
        qWarning() << "QRhi is null";
        return false;
    }

    QSize uvSize((m_size.width() + 1) / 2, (m_size.height() + 1) / 2);
    m_yTexture.reset(rhi->newTexture(QRhiTexture::R8, m_size, 1, QRhiTexture::Flags{}));
    m_uTexture.reset(rhi->newTexture(QRhiTexture::R8, uvSize, 1, QRhiTexture::Flags{}));
    m_vTexture.reset(rhi->newTexture(QRhiTexture::R8, uvSize, 1, QRhiTexture::Flags{}));
    if (!m_yTexture->create() || !m_uTexture->create() || !m_vTexture->create()) {
        qWarning() << "Failed to create YUV textures";
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
        qWarning() << "Failed to create sampler";
        return false;
    }

    m_vsUBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_vsUBuffer->create()) {
        qWarning() << "Failed to create vertex uniform buffer";
        return false;
    }
    m_fsColorBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_fsColorBuffer->create()) {
        qWarning() << "Failed to create fragment color buffer";
        return false;
    }
    m_fsYUVRangeBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float)*4*4));
    if (!m_fsYUVRangeBuffer->create()) {
        qWarning() << "Failed to create fragment YUV range buffer";
        return false;
    }

    m_srb.reset(rhi->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_vTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsUBuffer.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorBuffer.get()),
        QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVRangeBuffer.get())
    });
    if (!m_srb->create()) {
        qWarning() << "Failed to create shader resource bindings";
        return false;
    }
    
    return true;
}

void YuvShaderResource::updateVsUBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!m_vsUBuffer) {
        qWarning() << "Vertex uniform buffer is not initialized";
        return;
    }
    rub->updateDynamicBuffer(m_vsUBuffer.get(), 0, sizeof(float)*4*4, matrix.constData());
}

void YuvShaderResource::updateFsColorBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!m_fsColorBuffer) {
        qWarning() << "Fragment color buffer is not initialized";
        return;
    }
    rub->updateDynamicBuffer(m_fsColorBuffer.get(), 0, sizeof(float)*4*4, matrix.constData());
}

void YuvShaderResource::updateFsYUVRangeBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix)
{
    if (!m_fsYUVRangeBuffer) {
        qWarning() << "Fragment YUV range buffer is not initialized";
        return;
    }
    rub->updateDynamicBuffer(m_fsYUVRangeBuffer.get(), 0, sizeof(float)*4*4, matrix.constData());
}

void YuvShaderResource::updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame)
{
    if (!frame) {
        qWarning() << "Frame is null";
        return;
    }

    if (!m_yTexture || !m_uTexture || !m_vTexture) {
        qWarning() << "YUV textures are not initialized";
        return;
    }

    auto uploadTextureData = [&](QRhiTexture* texture, const uint8_t* data, int width, int height, int lineSize) {
        if (!data) {
            qWarning() << "Texture data is null";
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
    uploadTextureData(m_uTexture.get(), frame->uData(), (frame->width() + 1) / 2, (frame->height() + 1) / 2, frame->uLineSize());
    uploadTextureData(m_vTexture.get(), frame->vData(), (frame->width() + 1) / 2, (frame->height() + 1) / 2, frame->vLineSize());
}

} // namespace view