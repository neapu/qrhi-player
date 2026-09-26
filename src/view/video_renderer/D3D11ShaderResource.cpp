#include "D3D11ShaderResource.h"
#include <QDebug>

namespace view {
D3D11ShaderResource::D3D11ShaderResource(const Params& params)
    : ShaderResource(params)
    , m_d3d11Device(params.d3d11Device)
    , m_d3d11DeviceContext(params.d3d11DeviceContext)
{
    if (m_swPixelFormat == AV_PIX_FMT_NV12) {
        m_textureFormat = DXGI_FORMAT_NV12;
    } else if (m_swPixelFormat == AV_PIX_FMT_P010LE) {
        m_textureFormat = DXGI_FORMAT_P010;
    } else {
        m_textureFormat = DXGI_FORMAT_UNKNOWN;
    }
}

bool D3D11ShaderResource::initializeResources()
{
    if (!m_d3d11Device || !m_d3d11DeviceContext) {
        qCritical() << "D3D11 device or device context is null";
        return false;
    }

    qInfo() << "Initializing D3D11ShaderResource";
    if (m_textureFormat == DXGI_FORMAT_UNKNOWN) {
        qCritical() << "Unsupported texture format";
        return false;
    }

    m_nativeTexture.Reset();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_size.width();
    desc.Height = m_size.height();
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = m_textureFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT hr = m_d3d11Device->CreateTexture2D(&desc, nullptr, m_nativeTexture.GetAddressOf());
    if (FAILED(hr)) {
        qCritical() << "Failed to create D3D11 texture";
        return false;
    }

    // 创建QRhiTexture
    QRhiTexture::NativeTexture nativeTex{};
    nativeTex.object = reinterpret_cast<quint64>(m_nativeTexture.Get());
    nativeTex.layout = 0; // d3d11不需要

    QSize uvSize{(m_size.width() + 1) / 2, (m_size.height() + 1) / 2};
    if (m_textureFormat == DXGI_FORMAT_NV12) {
        m_yTexture.reset(m_rhi->newTexture(QRhiTexture::R8, m_size, 1, QRhiTexture::Flags()));
        m_uvTexture.reset(m_rhi->newTexture(QRhiTexture::RG8, uvSize, 1, QRhiTexture::Flags()));
    } else if (m_textureFormat == DXGI_FORMAT_P010) {
        m_yTexture.reset(m_rhi->newTexture(QRhiTexture::R16, m_size, 1, QRhiTexture::Flags()));
        m_uvTexture.reset(m_rhi->newTexture(QRhiTexture::RG16, uvSize, 1, QRhiTexture::Flags()));
    } else {
        qCritical() << "Unsupported texture format for SRB creation:" << static_cast<int>(m_textureFormat);
        return false;
    }

    if (!m_yTexture->createFrom(nativeTex) || !m_uvTexture->createFrom(nativeTex)) {
        qCritical() << "Failed to create QRhiTexture from native texture";
        return false;
    }

    m_sampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear, // 放大过滤，指像素放大时如何插值
        QRhiSampler::Linear,                               // 缩小过滤，指像素缩小时如何插值
        QRhiSampler::None,                                 // mipmap过滤，禁用了mipmap所以使用None
        QRhiSampler::ClampToEdge,                          // U方向(水平方向)的纹理坐标超出[0,1]范围时的处理方式
        QRhiSampler::ClampToEdge                           // V方向(垂直方向)的纹理坐标超出[0,1]范围时的处理方式
        ));
    if (!m_sampler->create()) {
        qCritical() << "Failed to create sampler";
        return false;
    }

    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uvTexture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
        QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())});
    if (!m_srb->create()) {
        qCritical() << "Failed to create shader resource bindings";
        return false;
    }

    return true;
}

void D3D11ShaderResource::updateTexture(QRhiResourceUpdateBatch* rub, const fh::FramePtr& frame)
{
    if (!frame) {
        qCritical() << "Frame is null";
        return;
    }

    D3D11_BOX srcBox{};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = static_cast<UINT>(frame->width);
    srcBox.bottom = static_cast<UINT>(frame->height);
    srcBox.back = 1;

    auto* texture = static_cast<ID3D11Texture2D*>(reinterpret_cast<void*>(frame->data[0]));
    if (!texture) {
        qCritical() << "Failed to get D3D11 texture from frame";
        return;
    }

    int subresourceIndex = static_cast<int>(reinterpret_cast<uintptr_t>(frame->data[1]));
    m_d3d11DeviceContext->CopySubresourceRegion(
        m_nativeTexture.Get(), 
        0, 0, 0, 0, 
        texture, 
        subresourceIndex, 
        &srcBox
    );
}

} // namespace view
