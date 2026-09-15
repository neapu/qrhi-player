#include "QRhiVideoRenderer.h"
#include <QFile>
#include "VideoRendererUtils.h"

namespace {
QShader loadShader(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open shader file:" << fileName;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}
}


namespace view {
    
QRhiVideoRenderer::QRhiVideoRenderer(QWidget* parent)
    : QRhiWidget(parent)
{
}

QRhiVideoRenderer::~QRhiVideoRenderer() = default;

void QRhiVideoRenderer::setGetFrameCallback(GetFrameCallback callback)
{
    m_getFrameCallback = std::move(callback);
}

void QRhiVideoRenderer::start()
{
    m_isRunning = true;
    update();
}

void QRhiVideoRenderer::stop()
{
    m_isRunning = false;
    m_getFrameCallback = nullptr;
    update();
}

void QRhiVideoRenderer::initialize(QRhiCommandBuffer *cb)
{
    if (m_rhi != rhi()) {
        m_rhi = rhi();
        m_vertexBuffer.reset();
        m_rhiPipeline.reset();
        m_yuvShaderResource.reset();
    }

    if (m_vertexBuffer) { // initialize() 只创建顶点缓冲区，其他资源在渲染时创建
        return;
    }

    qInfo() << "Using QRhi backend:" << m_rhi->backendName();

    const float vertices[] = {
		-1.0f, -1.0f, 0.0f, 1.0f,
		 1.0f, -1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f, 0.0f, 0.0f,
		 1.0f,  1.0f, 1.0f, 0.0f
	};

    // 创建顶点缓冲区（相当于OpenGL中的VBO）
    m_vertexBuffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertices))
    );
    if (!m_vertexBuffer->create()) {
        qWarning() << "Failed to create vertex buffer";
        m_vertexBuffer.reset();
        errorOccurred("Failed to create vertex buffer");
        return;
    }

    // 上传顶点数据到GPU
    auto* rub = m_rhi->nextResourceUpdateBatch();
    rub->uploadStaticBuffer(m_vertexBuffer.get(), vertices);
    cb->resourceUpdate(rub);
}

void QRhiVideoRenderer::render(QRhiCommandBuffer *cb)
{
    if (!m_vertexBuffer) {
        qWarning() << "Vertex buffer not initialized";
        errorOccurred("Vertex buffer not initialized");
        return;
    }

    if (!m_isRunning) {
        // 如果没有运行，就不渲染任何内容
        auto* rub = m_rhi->nextResourceUpdateBatch();
        cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
        cb->endPass();
        return;
    }

    if (!m_getFrameCallback) {
        qWarning() << "GetFrameCallback not set";
        errorOccurred("GetFrameCallback not set");
        return;
    }

    auto frame = m_getFrameCallback();
    if (!frame) {
        // 渲染时机还没到，保持当前画面不变
        update(); // 保持垂直同步刷新
        return;
    }

    if (frame->pixelFormat() != controller::IFrame::PixelFormat::YUV420P) {
        qWarning() << "Unsupported video frame format";
        update(); // 保持垂直同步刷新
        return;
    }

    m_currentFrame = std::move(frame);
    renderFrame(cb);

    update(); // 保持垂直同步刷新

    // bool clearFlag = false;
    // {
    //     QMutexLocker locker(&m_frameMutex);
    //     if (m_clearFlag) {
    //         m_currentFrame.reset();
    //         clearFlag = true;
    //         m_clearFlag = false;
    //     } else if (m_pendingFrame) {
    //         m_currentFrame = std::move(m_pendingFrame);
    //     }
    // }

    // if (clearFlag || !m_currentFrame) {
    //     // 清除渲染目标
    //     auto* rub = m_rhi->nextResourceUpdateBatch();
    //     cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
    //     cb->endPass();
    //     return;
    // }

    // renderFrame(cb);
}

void QRhiVideoRenderer::releaseResources()
{
    m_vertexBuffer.reset();
    m_rhiPipeline.reset();
    m_yuvShaderResource.reset();
    m_rhi = nullptr;
    QRhiWidget::releaseResources();
}

void QRhiVideoRenderer::renderFrame(QRhiCommandBuffer* cb)
{
    if (!createPipeline()) {
        qWarning() << "Failed to create pipeline for rendering frame";
        return;
    }

    QSize renderSize = renderTarget()->pixelSize();
    QSize videoSize{m_currentFrame->width(), m_currentFrame->height()};
    // 顶点变换矩阵，用于缩放画面保持宽高比
    QMatrix4x4 vertexMatrix = createAspectRatioMatrix(videoSize, renderSize);
    // color_range变换矩阵，得到标准的yuv值
    QMatrix4x4 colorRangeMatrix = createYuvRangeMatrix(m_currentFrame->colorRange());
    // yuv_to_rgb变换矩阵，得到标准的rgb值
    QMatrix4x4 yuvToRgbMatrix = createYuvToRgbMatrix(m_currentFrame->colorSpace());
    

    auto* rub = m_rhi->nextResourceUpdateBatch();

    // 更新纹理数据
    m_yuvShaderResource->updateTexture(rub, m_currentFrame);
    // 更新uniform缓冲区数据
    m_yuvShaderResource->updateVsUBuffer(rub, vertexMatrix);
    m_yuvShaderResource->updateFsYUVRangeBuffer(rub, colorRangeMatrix);
    m_yuvShaderResource->updateFsColorBuffer(rub, yuvToRgbMatrix);

    cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
    cb->setGraphicsPipeline(m_rhiPipeline.get());
    cb->setShaderResources(m_yuvShaderResource->srb());
    cb->setViewport(QRhiViewport(0.0f, 0.0f, static_cast<float>(renderSize.width()), static_cast<float>(renderSize.height())));

    const QRhiCommandBuffer::VertexInput vertexBindings[] = {
        { m_vertexBuffer.get(), 0 }
    };
    cb->setVertexInput(0, 1, vertexBindings);
    cb->draw(4);
    cb->endPass();
}

bool QRhiVideoRenderer::createPipeline()
{
    if (!m_currentFrame) {
        qWarning() << "No current frame to create pipeline for";
        return false;
    }
    QSize frameSize{m_currentFrame->width(), m_currentFrame->height()};
    if (m_rhiPipeline 
        && m_yuvShaderResource 
        && m_yuvShaderResource->size() == frameSize
        && m_rhiPipeline->renderPassDescriptor() == renderTarget()->renderPassDescriptor()
    ) {
        return true;
    }

    qInfo() << "Creating pipeline for frame size:" << frameSize;

    m_rhiPipeline.reset();

    m_yuvShaderResource = std::make_unique<YuvShaderResource>(frameSize);
    if (!m_yuvShaderResource->initialize(m_rhi)) {
        qWarning() << "Failed to initialize YUV shader resource";
        m_yuvShaderResource.reset();
        return false;
    }

    // 由qt工具链预编译的着色器文件(.qsb)加载顶点和片段着色器，详见CMakeLists.txt中qt_add_shaders
    auto vertexShader = loadShader(":/shaders/video.vert.qsb");
    auto fragmentShader = loadShader(":/shaders/yuv420p.frag.qsb");
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        qWarning() << "Failed to load shaders";
        m_yuvShaderResource.reset();
        return false;
    }

    // 顶点布局(类似OpenGL中的VAO)
    QRhiVertexInputLayout inputLayout{};
    inputLayout.setBindings({
        { sizeof(float) * 4 }   // 本项目只用了一个顶点缓冲区，同时描述顶点位置和纹理坐标，每个顶点共4个float
    });
    inputLayout.setAttributes({
        // 第一个参数表示绑定的顶点缓冲区的索引，因为只有一个顶点缓冲区，这里都设置为0
        // 第二个参数表示顶点着色器中对应的输入变量的位置(location)，这里假设顶点着色器中有两个输入变量，位置和纹理坐标，分别对应location 0和1
        // 第三个参数表示顶点属性的格式，这里使用Float2表示两个float组成的向量
        // 第四个参数表示顶点属性在顶点缓冲区中的偏移量，这里顶点位置在缓冲区的起始位置，所以偏移为0，纹理坐标在顶点位置之后，所以偏移为sizeof(float) * 2
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 }, // 顶点位置
        { 0, 1, QRhiVertexInputAttribute::Float2, sizeof(float) * 2 } // 纹理坐标
    });

    m_rhiPipeline.reset(m_rhi->newGraphicsPipeline());
    m_rhiPipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertexShader },
        { QRhiShaderStage::Fragment, fragmentShader }
    });
    m_rhiPipeline->setVertexInputLayout(inputLayout);
    // 绘制条带三角形，用四个顶点渲染两个三角形
    m_rhiPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_rhiPipeline->setShaderResourceBindings(m_yuvShaderResource->srb());
    m_rhiPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!m_rhiPipeline->create()) {
        qWarning() << "Failed to create graphics pipeline";
        m_rhiPipeline.reset();
        m_yuvShaderResource.reset();
        return false;
    }

    return true;
}

} // namespace view