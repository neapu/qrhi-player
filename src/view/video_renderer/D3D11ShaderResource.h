#pragma once
#include "ShaderResource.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace view {
class D3D11ShaderResource : public ShaderResource {
public:
    D3D11ShaderResource(const Params& params);

    QRhiShaderResourceBindings* shaderResourceBindings() const override { return m_srb.get(); }
    // 默认实现将操作排队进 rub;派生类允许立即提交，但必须满足 beginPass 前调用的时间契约
    void updateTexture(QRhiResourceUpdateBatch* rub, const fh::FramePtr& frame) override;
protected:
    bool initializeResources() override;

protected:
    std::unique_ptr<QRhiTexture> m_yTexture{nullptr};
    std::unique_ptr<QRhiTexture> m_uvTexture{nullptr};

    ID3D11Device* m_d3d11Device{nullptr};
    ID3D11DeviceContext* m_d3d11DeviceContext{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nativeTexture{nullptr};
    DXGI_FORMAT m_textureFormat{ DXGI_FORMAT_UNKNOWN };

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
};
} // namespace view