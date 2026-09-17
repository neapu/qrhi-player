#include "MainWindow.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include "LogManager.h"

namespace view {
MainWindow::MainWindow(const QString &commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    qInfo() << "MainWindow created";
    
    setWindowTitle("QRHI Player");
    resize(800, 600);

    createMenuBar();
    createCentralWidget();

    m_videoRenderer->setGetFrameCallback([this]() {
        return getVideoFrame();
    });

    if (!commandInputFile.isEmpty()) {
        openVideo(commandInputFile);
    }
}

MainWindow::~MainWindow()
{
}

void MainWindow::openVideo(const QString &videoFile)
{
    qInfo() << "Opening video file:" << videoFile;
    controller::IController::Params params{};
    params.url = videoFile.toStdString();
    params.logCallback = [](controller::LogLevel level, const std::string& fileName, int line, const std::string& message) {
        LogManager::instance().logControllerMessage(level, fileName, line, message);
    };
    m_controller = controller::IController::create(params);
    if (!m_controller) {
        qCritical() << "Failed to create controller for video file:" << videoFile;
        return;
    }
    if (auto audioParams = m_controller->audioParams()) {
        AudioRenderer::Params rendererParams{};
        rendererParams.sampleRate = audioParams->sampleRate;
        rendererParams.channels = audioParams->channels;
        rendererParams.frameCallback = [this] {
            return m_controller->nextAudioFrame();  // 由时序保证 m_controller 不为空
        };
        rendererParams.playingAudioPtsCallback = [this] (int64_t pts) {
            m_controller->audioRenderTime(pts);
        };
        m_audioRenderer = view::AudioRenderer::create(rendererParams);
        if (m_audioRenderer) {
            m_audioRenderer->setVolume(m_volume);
        }
    }

    m_videoRenderer->start();
}

void MainWindow::closeVideo()
{
    m_audioRenderer.reset();
    m_videoRenderer->stop();
    m_controller.reset();
}

void MainWindow::createMenuBar()
{
    auto* menu = this->menuBar()->addMenu(tr("File(&F)"));
    auto* openAction = menu->addAction(tr("Open(&O)"));
    connect(openAction, &QAction::triggered, this, [this]() {
        QString videoFile = QFileDialog::getOpenFileName(
            this, tr("Open Video"), QString(), tr("Video Files (*.mp4 *.avi *.mkv)"));
        if (!videoFile.isEmpty()) {
            closeVideo();
            openVideo(videoFile);
        }
    });

    auto* exitAction = menu->addAction(tr("Exit(&E)"));
    connect(exitAction, &QAction::triggered, this, [this]() {
        close();
    });
}

void MainWindow::createCentralWidget()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_videoRenderer = new view::QRhiVideoRenderer(centralWidget);
    connect(m_videoRenderer, &view::QRhiVideoRenderer::errorOccurred, this, &MainWindow::onVideoRendererError);
    layout->addWidget(m_videoRenderer);
}

void MainWindow::onVideoRendererError(const QString &error)
{
    QMessageBox::critical(this, tr("Video Renderer Error"), error);
    qFatal() << "Video renderer error:" << error;
    QApplication::exit(-1);
}

controller::FramePtr MainWindow::getVideoFrame()
{
    if (!m_controller) {
        return nullptr;
    }

    // 没有音频设备时，需要消费掉音频帧，避免阻塞视频渲染
    if (!m_audioRenderer) {
        while (m_controller->nextAudioFrame()) {
            // Consume audio frames without processing
        }
    }

    while (auto videoFrame = m_controller->nextVideoFrame()) {
        if (videoFrame->serial() < m_serial) {
            continue;
        }

        m_serial = videoFrame->serial();
        return videoFrame;
    }

    return nullptr;
}

} // namespace view