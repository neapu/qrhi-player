#pragma once
#include <QSize>
#include <rhi/qrhi.h>
#include <QMatrix4x4>
#include <memory>
#include <controller/Frame.h>

namespace view {
class ShaderResource {
public:
    enum class Type {
        Yuv,
    };

    struct Params {
        Type type;
        QRhi* rhi;
        QSize size;
    };
    static std::unique_ptr<ShaderResource> create(const Params& params);

    virtual ~ShaderResource() = default;

    virtual QSize size() const = 0;
    virtual QRhiShaderResourceBindings* shaderResourceBinding() const = 0;

    virtual void updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) = 0;
    virtual void updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) = 0;
    virtual void updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix) = 0;

    virtual void updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame) = 0;
protected:
    virtual bool initialize(const Params& params) = 0;
};
} // namespace view