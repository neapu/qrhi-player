#pragma once
#include <QRhiWidget>
#include <functional>
#include <rhi/qrhi.h>
#include "controller/Frame.h"
#include "ShaderResource.h"

namespace view {
// 以垂直同步的速度拉取待渲染的帧，如果没到渲染时间，返回nullptr，表示不需要渲染新帧
using GetFrameCallback = std::function<controller::FramePtr()>;

// 线程契约：所有方法都必须在GUI线程中调用
// 调用顺序：setGetFrameCallback -> start -> stop ---> release
class QRhiVideoRenderer : public QRhiWidget {
    Q_OBJECT
public:
    explicit QRhiVideoRenderer(QWidget *parent = nullptr);
    ~QRhiVideoRenderer();

    void setGetFrameCallback(GetFrameCallback callback);
    // 开始垂直同步拉取视频帧渲染
    void start();
    // 停止垂直同步刷新
    void stop();
    void clear();

signals:
    void errorOccurred(const QString &errorMessage);
    // 拉取到End帧（播放到流结尾）时触发，每次start()后最多触发一次
    void playbackFinished();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    bool createPipeline(ShaderResource::Type type, const controller::FramePtr& frame);
    void renderFrame(QRhiCommandBuffer* cb);
protected:
    bool m_running{false};
    bool m_endReached{false}; // End帧只投递一次，这里再兜一层，保证playbackFinished不重复触发
    GetFrameCallback m_getFrameCallback{};
    QRhi* m_rhi{nullptr};

    std::unique_ptr<QRhiBuffer> m_vertexBuffer{nullptr};
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline{nullptr};
    std::unique_ptr<ShaderResource> m_shaderResource{nullptr};

    controller::FramePtr m_currentFrame{nullptr};
};

} // namespace view