#pragma once
#include "ShaderResource.h"

namespace view {
class YuvShaderResource : public ShaderResource {
public:
    static std::unique_ptr<YuvShaderResource> create(const Params& params);

    ~YuvShaderResource() override = default;

    QSize size() const override { return m_size; }
    QRhiShaderResourceBindings* shaderResourceBinding() const override { return m_srb.get(); }

    void updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) override;
    void updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) override;
    void updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) override;

    void updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame) override;

protected:
    bool initialize(const Params& params) override;

protected:
    QSize m_size;
    std::unique_ptr<QRhiTexture> m_yTexture;
    std::unique_ptr<QRhiTexture> m_uTexture;
    std::unique_ptr<QRhiTexture> m_vTexture;

    std::unique_ptr<QRhiSampler> m_sampler;

    std::unique_ptr<QRhiShaderResourceBindings> m_srb;

    std::unique_ptr<QRhiBuffer> m_vsVertexTransformMatrix; // 对应video.vert中的UBuf变量，绑定位置3
    std::unique_ptr<QRhiBuffer> m_fsColorRangeConversionMatrix; // 对应yuv420p.frag中的ColorRangeBlock变量，绑定位置4，在着色器中先乘
    std::unique_ptr<QRhiBuffer> m_fsYUVtoRGBMatrix; // 对应yuv420p.frag中的YuvToRGBBlock变量，绑定位置5，在着色器中后乘
};
} // namespace view