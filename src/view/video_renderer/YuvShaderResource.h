#pragma once
#include <QSize>
#include <rhi/qrhi.h>
#include <memory>
#include "controller/Frame.h"

namespace view {
class YuvShaderResource {
public:
    explicit YuvShaderResource(const QSize& size);

    bool initialize(QRhi* rhi);
    QRhiShaderResourceBindings* srb() const { return m_srb.get(); }

    QSize size() const { return m_size; }

    void updateVsUBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);
    void updateFsColorBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);
    void updateFsYUVRangeBuffer(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);

    void updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame);

private:
    QSize m_size;
    std::unique_ptr<QRhiTexture> m_yTexture;
    std::unique_ptr<QRhiTexture> m_uTexture;
    std::unique_ptr<QRhiTexture> m_vTexture;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;

    std::unique_ptr<QRhiBuffer> m_vsUBuffer; // 对应video.vert中的UBuf变量，绑定位置3
    std::unique_ptr<QRhiBuffer> m_fsColorBuffer; // 对应yuv420p.frag中的ColorConversionBlock变量，绑定位置4
    std::unique_ptr<QRhiBuffer> m_fsYUVRangeBuffer; // 对应yuv420p.frag中的YUVRangeBlock变量，绑定位置5
};

} // namespace view
