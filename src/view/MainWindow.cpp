#include "MainWindow.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include "LogManager.h"

namespace {
QPushButton* createIconButton(const QIcon& icon, const QString& tooltip, QWidget* parent = nullptr)
{
    auto* button = new QPushButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(QSize(32, 32));
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// 裸图标按钮：不带按钮边框和背景，看上去只是一个可点击的图标
QToolButton* createBareIconButton(const QIcon& icon, const QString& tooltip, QWidget* parent = nullptr)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(QSize(24, 24));
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);  // 常态无边框，仅悬停/按下时有轻微高亮
    button->setStyleSheet("QToolButton { border: none; }");
    return button;
}

// 格式化为 mm:ss 形式
QString formatTime(double seconds)
{
    int totalSeconds = static_cast<int>(seconds);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}
QString formatTime(int64_t ptsUs)
{
    return formatTime(static_cast<double>(ptsUs) / 1000000.0);
}

}

namespace view {
MainWindow::MainWindow(const QString &commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    qInfo() << "MainWindow created";
    
    setWindowTitle("QRHI Player");
    resize(800, 600);

    // 必须先创建 QRhiWidget 子部件，再创建菜单栏。
    // macOS 上 QMenuBar 是原生菜单栏，QMenuBarPrivate::handleReparent() 会调用
    // newWindow->createWinId() 提前创建顶层窗口，而窗口在创建时会通过
    // q_evaluateRhiConfig() 遍历子部件决定是否使用 RHI 合成（Metal）。
    // 如果此时 QRhiVideoRenderer 还不存在，窗口就会退化为光栅（QCALayerBackingStore）合成，
    // 之后再创建的 QRhiWidget 拿不到 QRhi，只会不断报 "QRhiWidget: No QRhi"。
    createCentralWidget();
    createMenuBar();

    m_videoRenderer->setGetFrameCallback([this]() {
        return getVideoFrame();
    });

    if (!commandInputFile.isEmpty()) {
        openVideo(commandInputFile);
    }
}

MainWindow::~MainWindow()
{
    closeVideo();
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
            QMetaObject::invokeMethod(this, &MainWindow::onPlayback, Qt::QueuedConnection, pts);
        };
        m_audioRenderer = view::AudioRenderer::create(rendererParams);
        if (m_audioRenderer) {
            m_audioRenderer->setVolume(m_volume);
        }
        connect(m_audioRenderer.get(), &view::AudioRenderer::playbackFinished, this, [this]() {
            m_audioEndFlag = true;
            if (m_videoEndFlag) {
                setState(State::Stopped);
            }
        });
    }
    double duration = m_controller->duration();
    if (duration > 0) {
        m_playbackSlider->setEnabled(true);
        m_playbackSlider->setMaximum(static_cast<int>(duration * 1000));  // 将滑块的最大值设置为视频时长（毫秒）
    }
    m_durationText = formatTime(duration);
    m_playbackLabel->setText(QString("00:00 / %1").arg(m_durationText));

    m_serial = 0;
    m_videoEndFlag = false;
    m_audioEndFlag = false;

    m_videoRenderer->start();
    setState(State::Playing);
}

void MainWindow::closeVideo()
{
    m_audioRenderer.reset();
    m_videoRenderer->stop();
    m_controller.reset();
    m_playbackSlider->setEnabled(false);
    setState(State::Idle);
}

void MainWindow::createMenuBar()
{
    auto* menu = this->menuBar()->addMenu(tr("File(&F)"));
    auto* openAction = menu->addAction(tr("Open(&O)"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

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
    connect(m_videoRenderer, &view::QRhiVideoRenderer::playbackFinished, this, [this]() {
        m_videoEndFlag = true;
        if (m_audioEndFlag) {
            setState(State::Stopped);
        }
    });
    layout->addWidget(m_videoRenderer, 1);
    createControlWidgets(layout, centralWidget);
}

void MainWindow::createControlWidgets(QBoxLayout* layout, QWidget* parent)
{
    // 中央布局本身不受边距约束（视频需要贴满），
    // 因此控件区自己的内边距在这里单独设置。
    QHBoxLayout* slidersLayout = new QHBoxLayout();
    slidersLayout->setContentsMargins(8, 6, 8, 3);
    slidersLayout->setSpacing(8);

    m_playbackSlider = new QSlider(Qt::Horizontal, parent);
    m_playbackSlider->setEnabled(false);
    connect(m_playbackSlider, &QSlider::sliderPressed, this, &MainWindow::onPlaybackSliderPressed);
    connect(m_playbackSlider, &QSlider::sliderReleased, this, &MainWindow::onPlaybackSliderReleased);
    slidersLayout->addWidget(m_playbackSlider);

    m_volumeMuteIcon = QIcon(":/icons/volume-mute.svg");
    m_volumeDownIcon = QIcon(":/icons/volume-down.svg");
    m_volumeUpIcon = QIcon(":/icons/volume-up.svg");
    m_volumeIconButton = createBareIconButton(m_volumeUpIcon, tr("静音"), parent);
    connect(m_volumeIconButton, &QToolButton::clicked, this, &MainWindow::onVolumeIconClicked);
    slidersLayout->addWidget(m_volumeIconButton);

    m_volumeSlider = new QSlider(Qt::Horizontal, parent);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(static_cast<int>(m_volume * 100));
    m_volumeSlider->setFixedWidth(100);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_volume = static_cast<float>(value) / 100.0f;
        if (m_audioRenderer || !m_audioRenderer) {
            m_audioRenderer->setVolume(m_volume);
        }
        updateVolumeIcon();
    });
    slidersLayout->addWidget(m_volumeSlider);

    layout->addLayout(slidersLayout);

    m_playIcon = QIcon(":/icons/play.svg");
    m_pauseIcon = QIcon(":/icons/pause.svg");
    m_stopIcon = QIcon(":/icons/stop.svg");
    QHBoxLayout* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setContentsMargins(8, 3, 8, 8);
    buttonsLayout->setSpacing(8);
    m_playOrPauseButton = createIconButton(m_playIcon, tr("Play/Pause"), parent);
    buttonsLayout->addWidget(m_playOrPauseButton);
    connect(m_playOrPauseButton, &QPushButton::clicked, this, &MainWindow::onPlayOrPauseClicked);
    m_stopButton = createIconButton(m_stopIcon, tr("Stop"), parent);
    buttonsLayout->addWidget(m_stopButton);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    buttonsLayout->addStretch();
    m_playbackLabel = new QLabel(tr("00:00 / 00:00"), parent);
    buttonsLayout->addWidget(m_playbackLabel);
    
    layout->addLayout(buttonsLayout);
}

void MainWindow::updateVolumeIcon()
{
    if (m_volume <= 0.0f) {
        m_volumeIconButton->setIcon(m_volumeMuteIcon);
        m_volumeIconButton->setToolTip(tr("取消静音"));
    } else {
        m_volumeIconButton->setIcon(m_volume < 0.5f ? m_volumeDownIcon : m_volumeUpIcon);
        m_volumeIconButton->setToolTip(tr("静音"));
    }
}

void MainWindow::openFile()
{
    QString videoFile = QFileDialog::getOpenFileName(
        this, tr("Open Video"), QString(), tr("Video Files (*.mp4 *.avi *.mkv)"));
    if (!videoFile.isEmpty()) {
        closeVideo();
        openVideo(videoFile);
    }
}

void MainWindow::onVolumeIconClicked()
{
    if (m_volume > 0.0f) {
        m_volumeBeforeMute = m_volume;
        m_volumeSlider->setValue(0);
    } else {
        // 静音前的音量为 0（例如启动时就是 0），退化为恢复满音量
        const float restored = m_volumeBeforeMute > 0.0f ? m_volumeBeforeMute : 1.0f;
        m_volumeSlider->setValue(static_cast<int>(restored * 100.0f));
    }
    // setValue 数值未变化时不会发 valueChanged，这里兜底刷新一次图标
    updateVolumeIcon();
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
        // 没有音频时，需要由视频报告播放进度
        if (!m_audioRenderer) {
            // 不用切换线程，本来就在GUI线程中
            int64_t ptsUs = videoFrame->pts();
            if (ptsUs != INT64_MIN) {
                onPlayback(ptsUs);
            }
        }
        return videoFrame;
    }

    return nullptr;
}

void MainWindow::setState(State state)
{
    m_state = state;
    switch (m_state) {
        case State::Idle:
            m_playOrPauseButton->setIcon(m_playIcon);
            break;
        case State::Playing:
            m_playOrPauseButton->setIcon(m_pauseIcon);
            break;
        case State::Paused:
            m_playOrPauseButton->setIcon(m_playIcon);
            break;
        case State::Stopped:
            m_playOrPauseButton->setIcon(m_stopIcon);
            break;
    }
}

void MainWindow::onPlayOrPauseClicked()
{
    switch (m_state) {
        case State::Idle:
            openFile();
            break;
        case State::Playing:
            if (!m_controller->isPaused()) m_controller->pauseOrResume();
            setState(State::Paused);
            break;
        case State::Paused:
            if (m_controller->isPaused()) m_controller->pauseOrResume();
            setState(State::Playing);
            break;
        case State::Stopped:
            m_controller->seek(0);
            if (m_controller->isPaused()) {
                m_controller->pauseOrResume();
            }
            setState(State::Playing);
            break;
    };
}

void MainWindow::onStopClicked()
{
    if (m_state != State::Idle) {
        closeVideo();
    }
}

void MainWindow::onPlaybackSliderPressed()
{
    m_isPlaybackSliderPressed = true;
}

void MainWindow::onPlaybackSliderReleased()
{
    m_isPlaybackSliderPressed = false;
    m_controller->seek(static_cast<double>(m_playbackSlider->value()) / 1000.0);
}

void MainWindow::onPlayback(int64_t ptsUs)
{
    if (!m_isPlaybackSliderPressed) {
        if (m_playbackSlider->isEnabled()) {
            m_playbackSlider->setValue(static_cast<int>(ptsUs / 1000));
        }
        m_playbackLabel->setText(QString("%1 / %2").arg(formatTime(ptsUs)).arg(m_durationText));
    }
}

} // namespace view