#pragma once
#include <QRhiWidget>
#include <functional>
#include <rhi/qrhi.h>
#include "controller/Frame.h"
#include "YuvShaderResource.h"

namespace view {
// 以垂直同步的速度拉取待渲染的帧，如果没到渲染时间，返回nullptr，表示不需要渲染新帧
using GetFrameCallback = std::function<controller::FramePtr()>;

// 线程契约：所有方法都必须在GUI线程中调用
// 调用顺序：setGetFrameCallback -> start -> stop ---> release
class QRhiVideoRenderer final : public QRhiWidget {
    Q_OBJECT
public:
    explicit QRhiVideoRenderer(QWidget* parent = nullptr);
    ~QRhiVideoRenderer() override;

    void setGetFrameCallback(GetFrameCallback callback);

public slots:
    void start();
    void stop();

signals:
    void errorOccurred(const QString& errorMessage);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    void renderFrame(QRhiCommandBuffer* cb);
    bool createPipeline();

private:
    GetFrameCallback m_getFrameCallback{};
    bool m_isRunning{false};
    controller::FramePtr m_currentFrame{};

    QRhi* m_rhi{nullptr};
    std::unique_ptr<QRhiGraphicsPipeline> m_rhiPipeline{};
    std::unique_ptr<QRhiBuffer> m_vertexBuffer{};
    std::unique_ptr<YuvShaderResource> m_yuvShaderResource{};
};

} // namespace view
