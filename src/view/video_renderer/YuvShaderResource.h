#pragma once
#include "ShaderResource.h"

namespace view {
class YuvShaderResource : public ShaderResource {
public:
    YuvShaderResource(const Params& params);
    ~YuvShaderResource() override = default;

    QRhiShaderResourceBindings* shaderResourceBinding() const override { return m_srb.get(); }

    void updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame) override;

protected:
    // 只初始化 YUV 纹理/采样器/SRB；矩阵 uniform buffer 由基类 initialize() 统一创建
    bool initializeResources() override;

protected:
    std::unique_ptr<QRhiTexture> m_yTexture;
    std::unique_ptr<QRhiTexture> m_uTexture;
    std::unique_ptr<QRhiTexture> m_vTexture;
    std::unique_ptr<QRhiTexture> m_uvTexture;

    std::unique_ptr<QRhiSampler> m_sampler;

    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
};
} // namespace view