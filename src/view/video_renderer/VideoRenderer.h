#pragma once
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include "ShaderResource.h"
#include "Frame.h"
#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace view {
using FrameCallback = std::function<fh::FramePtr()>;
class VideoRenderer : public QRhiWidget {
    Q_OBJECT
public:
    explicit VideoRenderer(FrameCallback frameCallback, QWidget *parent = nullptr);
    ~VideoRenderer() override;

    void clear();

#ifdef _WIN32
    ID3D11Device* d3d11Device() { return m_d3d11Device; }
#endif

signals:
    void errorOccurred(const QString &error);
    void initialized();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    void updatePipeline(QRhiResourceUpdateBatch* rub, fh::FramePtr&& frame);
    void createPipeline(const fh::FramePtr& frame);

protected:
    FrameCallback m_frameCallback;
    QRhi* m_rhi{nullptr};
    std::unique_ptr<QRhiBuffer> m_vertexBuffer{nullptr};
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline{nullptr};
    std::unique_ptr<ShaderResource> m_shaderResource{nullptr};

#ifdef _WIN32
    // 裸指针安全声明：从QRhi上下文内部获取，生命周期由QRhi管理
    ID3D11Device* m_d3d11Device{nullptr};
    ID3D11DeviceContext* m_d3d11DeviceContext{nullptr};
#endif
};

} // namespace view