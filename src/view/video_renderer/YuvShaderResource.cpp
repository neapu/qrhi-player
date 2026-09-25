#include "YuvShaderResource.h"
#include <QDebug>

namespace view {
YuvShaderResource::YuvShaderResource(const Params& params)
    : ShaderResource(params)
{
}

void YuvShaderResource::updateTexture(QRhiResourceUpdateBatch* rub, const fh::FramePtr& frame)
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

    if (m_swPixelFormat == AV_PIX_FMT_YUV420P || m_swPixelFormat == AV_PIX_FMT_YUV420P10LE) {
        uploadTextureData(m_yTexture.get(), frame->data[0], frame->width, frame->height, frame->linesize[0]);
        uploadTextureData(m_uTexture.get(), frame->data[1], (frame->width + 1) / 2, (frame->height + 1) / 2, frame->linesize[1]);
        uploadTextureData(m_vTexture.get(), frame->data[2], (frame->width + 1) / 2, (frame->height + 1) / 2, frame->linesize[2]);
    } else if (m_swPixelFormat == AV_PIX_FMT_NV12 || m_swPixelFormat == AV_PIX_FMT_P010LE) {
        uploadTextureData(m_yTexture.get(), frame->data[0], frame->width, frame->height, frame->linesize[0]);
        uploadTextureData(m_uvTexture.get(), frame->data[1], (frame->width + 1) / 2, (frame->height + 1) / 2, frame->linesize[1]);
    } else {
        qCritical() << "Unsupported pixel format";
    }
}

bool YuvShaderResource::initializeResources()
{
    if (m_type != Type::Yuv) {
        qCritical() << "called with incorrect type";
        return false;
    }

    // m_rhi / m_size 已由基类 ShaderResource::initialize() 校验并赋值，
    // 矩阵 uniform buffer 也已由基类创建

    m_sampler.reset(m_rhi->newSampler(
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

    

    // 初始化纹理
    QSize uvSize((m_size.width() + 1) / 2, (m_size.height() + 1) / 2);
    if (m_swPixelFormat == AV_PIX_FMT_YUV420P || m_swPixelFormat == AV_PIX_FMT_YUV420P10LE) {
        QRhiTexture::Format format = (m_swPixelFormat == AV_PIX_FMT_YUV420P) ? QRhiTexture::R8 : QRhiTexture::R16;
        m_yTexture.reset(m_rhi->newTexture(format, m_size, 1, QRhiTexture::Flags{}));
        m_uTexture.reset(m_rhi->newTexture(format, uvSize, 1, QRhiTexture::Flags{}));
        m_vTexture.reset(m_rhi->newTexture(format, uvSize, 1, QRhiTexture::Flags{}));
        if (!m_yTexture->create() || !m_uTexture->create() || !m_vTexture->create()) {
            qCritical() << "failed to create YUV textures";
            return false;
        }

        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_vTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())
        });
    } else if (m_swPixelFormat == AV_PIX_FMT_NV12 || m_swPixelFormat == AV_PIX_FMT_P010LE) {
        QRhiTexture::Format yFormat = (m_swPixelFormat == AV_PIX_FMT_NV12) ? QRhiTexture::R8 : QRhiTexture::R16;
        QRhiTexture::Format uvFormat = (m_swPixelFormat == AV_PIX_FMT_NV12) ? QRhiTexture::RG8 : QRhiTexture::RG16;
        m_yTexture.reset(m_rhi->newTexture(yFormat, m_size, 1, QRhiTexture::Flags{}));
        m_uvTexture.reset(m_rhi->newTexture(uvFormat, uvSize, 1, QRhiTexture::Flags{}));
        if (!m_yTexture->create() || !m_uvTexture->create()) {
            qCritical() << "failed to create NV12/P010LE textures";
            return false;
        }

        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uvTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())
        });
    } else {
        qCritical() << "Unsupported pixel format";
        return false;
    }

    if (!m_srb->create()) {
        qCritical() << "Failed to create shader resource bindings";
        return false;
    }

    return true;
}

} // namespace view