#include "QRhiVideoRenderer.h"
#include <QFile>
#include <QDebug>
#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace {
QShader loadShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open shader file:" << path;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

QString pixelFormatToShaderName(controller::IFrame::PixelFormat pixelFormat)
{
    switch (pixelFormat) {
        case controller::IFrame::PixelFormat::YUV420P:
        case controller::IFrame::PixelFormat::YUV420P10LE:
            return ":/shaders/yuv420p.frag.qsb";
        case controller::IFrame::PixelFormat::NV12:
        case controller::IFrame::PixelFormat::P010LE:
            return ":/shaders/nv12.frag.qsb";
        default:
            return "";
    }
}

// 创建顶点变换矩阵，负责保持宽高比
QMatrix4x4 createVertexTransformMatrix(const QSize& frameSize, const QSize& viewportSize)
{
    QMatrix4x4 matrix;
    if (frameSize.width() <= 0 || frameSize.height() <= 0
        || viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return matrix;
    }

    const float videoAspect = static_cast<float>(frameSize.width()) / frameSize.height();
    const float viewportAspect = static_cast<float>(viewportSize.width()) / viewportSize.height();
    if (viewportAspect > videoAspect) {
        matrix.scale(videoAspect / viewportAspect, 1.0f);
    } else {
        matrix.scale(1.0f, viewportAspect / videoAspect);
    }
    return matrix;
}

// 色彩范围转换矩阵，用于将limited range的YUV转换为full range的RGB
QMatrix4x4 createColorRangeConversionMatrix(controller::IFrame::ColorRange colorRange)
{
    static const QMatrix4x4 limitedRange{
        255.0f / 219.0f, 0.0f,          0.0f,           -16.0f / 219.0f,
        0.0f,           255.0f / 224.0f, 0.0f,          -128.0f / 224.0f,
        0.0f,           0.0f,           255.0f / 224.0f, -128.0f / 224.0f,
        0.0f,           0.0f,           0.0f,            1.0f
    };
    static const QMatrix4x4 fullRange{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 1.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return colorRange == controller::IFrame::ColorRange::Full ? fullRange : limitedRange;
}

// YUV转RGB的转换矩阵
QMatrix4x4 createYUVtoRGBMatrix(controller::IFrame::ColorSpace colorSpace)
{
    static const QMatrix4x4 bt601{
        1.0f,  0.0f,       1.402f,    0.0f,
        1.0f, -0.344136f, -0.714136f, 0.0f,
        1.0f,  1.772f,     0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };
    static const QMatrix4x4 bt709{
        1.0f,  0.0f,       1.5748f,   0.0f,
        1.0f, -0.187324f, -0.468124f, 0.0f,
        1.0f,  1.8556f,    0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };

    return colorSpace == controller::IFrame::ColorSpace::BT709 ? bt709 : bt601;
}

} // namespace

namespace view {

QRhiVideoRenderer::QRhiVideoRenderer(QWidget* parent) : QRhiWidget(parent) {}

QRhiVideoRenderer::~QRhiVideoRenderer() {}

void QRhiVideoRenderer::setGetFrameCallback(GetFrameCallback callback)
{
    m_getFrameCallback = callback;
}

void QRhiVideoRenderer::start()
{
    qInfo() << "Starting QRhiVideoRenderer.";
    m_running = true;
    m_endReached = false; // 重新开始播放，允许结尾信号再次触发
    update();
}

void QRhiVideoRenderer::stop()
{
    qInfo() << "Stopping QRhiVideoRenderer.";
    m_running = false;
}

void QRhiVideoRenderer::clear()
{
    m_currentFrame.reset();
    m_pipeline.reset();
    m_shaderResource.reset();
}

void QRhiVideoRenderer::initialize(QRhiCommandBuffer* cb)
{
    bool needReinitialize = false;
    if (m_rhi != this->rhi()) {
        m_rhi = rhi();
        needReinitialize = true;
    }
    if (!m_rhi) {
        qCritical() << "Failed to obtain QRhi instance.";
        emit errorOccurred("Failed to obtain QRhi instance.");
        return;
    }

    if (!needReinitialize) {
        return;
    }

    qInfo() << "Initializing QRhiVideoRenderer with new QRhi instance.";
    qInfo() << "Using QRhi backend:" << m_rhi->backendName();

#ifdef _WIN32
    if (m_rhi->backend() == QRhi::D3D11) {
        auto* nativeHandle = reinterpret_cast<const QRhiD3D11NativeHandles*>(m_rhi->nativeHandles());
        if (!nativeHandle) {
            qCritical() << "Failed to obtain D3D11 native handles.";
            emit errorOccurred("Failed to obtain D3D11 native handles.");
            return;
        }
        auto* device = static_cast<ID3D11Device*>(nativeHandle->dev);
        auto* context = static_cast<ID3D11DeviceContext*>(nativeHandle->context);
        
        // 启用多线程支持
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
            m_d3d11Device = device;
            m_d3d11DeviceContext = context;
        } else {
            // 回退到渲染软件帧模式，不赋值m_d3d11Device和m_d3d11DeviceContext
            qWarning() << "Failed to enable multithread protection for D3D11 device. Falling back to software rendering mode.";
        }
    }
#endif

    // 纹理坐标原点在左上角，而QRhi的默认坐标原点在左下角，所以需要翻转v坐标
    const float vertexData[] = {
        // x, y, u, v
        -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
        1.0f, -1.0f, 1.0f, 1.0f,  // bottom-right
        -1.0f, 1.0f, 0.0f, 0.0f,  // top-left
        1.0f, 1.0f, 1.0f, 0.0f    // top-right
    };
    m_vertexBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertexData)));
    if (!m_vertexBuffer->create()) {
        qWarning() << "Failed to create vertex buffer.";
        m_vertexBuffer.reset();
        m_rhi = nullptr;
        emit errorOccurred("Failed to create vertex buffer.");
        return;
    }

    // 上传顶点数据到GPU
    auto* rub = m_rhi->nextResourceUpdateBatch();
    rub->uploadStaticBuffer(m_vertexBuffer.get(), vertexData);
    cb->resourceUpdate(rub);

    qInfo() << "QRhiVideoRenderer initialized.";
    emit initialized();
}

void QRhiVideoRenderer::render(QRhiCommandBuffer* cb)
{
    if (!m_vertexBuffer) {
        return;
    }

    if (!m_getFrameCallback) {
        qCritical() << "GetFrameCallback is not set.";
        emit errorOccurred("GetFrameCallback is not set.");
        return;
    }
    controller::FramePtr frame = m_getFrameCallback();
    
    bool newFrame = false;
    if (frame && frame->type() == controller::IFrame::FrameType::End) {
        if (!m_endReached) {
            m_endReached = true;
            qInfo() << "Video playback reached the end of stream.";
            emit playbackFinished();
        }
    } else if (frame) {
        m_currentFrame = frame;
        newFrame = true;
    }

    if (newFrame) {
        renderFrame(cb);
    } else if (!m_currentFrame) {
        // 清屏
        auto* rub = m_rhi->nextResourceUpdateBatch();
        cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
        cb->endPass();
    }

    if (m_running) {
        update();
    }
}

void QRhiVideoRenderer::releaseResources()
{
    m_vertexBuffer.reset();
    m_pipeline.reset();
    m_shaderResource.reset();
    m_rhi = nullptr;
#ifdef _WIN32
    m_d3d11Device = nullptr;
    m_d3d11DeviceContext = nullptr;
#endif
    QRhiWidget::releaseResources();
}

bool QRhiVideoRenderer::createPipeline(ShaderResource::Type type, const controller::FramePtr& frame)
{
    QSize frameSize{frame->width(), frame->height()};
    if (m_pipeline 
        && m_shaderResource
        && m_shaderResource->size() == frameSize
        && m_shaderResource->type() == type
        && m_shaderResource->swPixelFormat() == frame->swPixelFormat()
        && m_pipeline->renderPassDescriptor() == renderTarget()->renderPassDescriptor()
    ) {
        return true;
    }
    
    qInfo() << "Creating pipeline for frame size:" << frameSize;

#ifdef _WIN32
    if (type == ShaderResource::Type::D3D11 && (!m_d3d11Device || !m_d3d11DeviceContext)) {
        qWarning() << "D3D11 device or context is not available.";
        return false;
    }
#endif

    m_pipeline.reset();

    ShaderResource::Params params;
    params.type = type;
    params.rhi = m_rhi;
    params.size = frameSize;
#ifdef _WIN32
    params.d3d11Device = m_d3d11Device;
    params.d3d11DeviceContext = m_d3d11DeviceContext;
#endif
    params.swPixelFormat = frame->swPixelFormat();
    m_shaderResource = ShaderResource::create(params);
    if (!m_shaderResource) {
        qWarning() << "Failed to create shader resource.";
        return false;
    }

    auto vertexShader = loadShader(":/shaders/video.vert.qsb");
    auto fragmentShader = loadShader(pixelFormatToShaderName(frame->swPixelFormat()));
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        qWarning() << "Failed to load shaders.";
        m_shaderResource.reset();
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

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setVertexInputLayout(inputLayout);
    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertexShader },
        { QRhiShaderStage::Fragment, fragmentShader }
    });
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setShaderResourceBindings(m_shaderResource->shaderResourceBinding());
    // 绘制条带三角形，用四个顶点渲染两个三角形
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    if (!m_pipeline->create()) {
        qWarning() << "Failed to create graphics pipeline.";
        m_shaderResource.reset();
        m_pipeline.reset();
        return false;
    }

    return true;
}

void QRhiVideoRenderer::renderFrame(QRhiCommandBuffer* cb)
{
    ShaderResource::Type shaderType = ShaderResource::Type::Yuv;
    auto frame = m_currentFrame;
    if (frame->pixelFormat() == controller::IFrame::PixelFormat::YUV420P
        || frame->pixelFormat() == controller::IFrame::PixelFormat::NV12
        || frame->pixelFormat() == controller::IFrame::PixelFormat::YUV420P10LE
        || frame->pixelFormat() == controller::IFrame::PixelFormat::P010LE
    ) {
        shaderType = ShaderResource::Type::Yuv;
    } else if (frame->pixelFormat() == controller::IFrame::PixelFormat::D3D11) {
        shaderType = ShaderResource::Type::D3D11;
    } else {
        qWarning() << "Unsupported pixel format.";
        return;
    }

    if (!createPipeline(shaderType, frame)) {
        return;
    }
    
    QSize renderSize = renderTarget()->pixelSize();
    QSize frameSize{frame->width(), frame->height()};
    QMatrix4x4 vertexTransformMatrix = createVertexTransformMatrix(frameSize, renderSize);
    QMatrix4x4 colorRangeConversionMatrix = createColorRangeConversionMatrix(frame->colorRange());
    QMatrix4x4 yuvToRGBMatrix = createYUVtoRGBMatrix(frame->colorSpace());

    auto* rub = m_rhi->nextResourceUpdateBatch();

    // 1. 更新纹理数据
    m_shaderResource->updateTexture(rub, frame);

    // 2. 更新uniform缓冲区数据
    m_shaderResource->updateVertexTransformMatrix(rub, vertexTransformMatrix);
    m_shaderResource->updateColorRangeConversionMatrix(rub, colorRangeConversionMatrix);
    m_shaderResource->updateYUVtoRGBMatrix(rub, yuvToRGBMatrix);

    // 3. 开始渲染
    cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setShaderResources(m_shaderResource->shaderResourceBinding());
    cb->setViewport(QRhiViewport(0.0f, 0.0f, static_cast<float>(renderSize.width()), static_cast<float>(renderSize.height())));

    const QRhiCommandBuffer::VertexInput vertexInput[] {
        { m_vertexBuffer.get(), 0 }
    };
    cb->setVertexInput(0, 1, vertexInput);
    cb->draw(4);
    cb->endPass();
}

} // namespace view
