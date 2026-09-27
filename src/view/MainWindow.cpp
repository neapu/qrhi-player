#include "MainWindow.h"
#include <QDebug>
#include <QVBoxLayout>

namespace view {

MainWindow::MainWindow(const QString& commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    m_commandInputFile = commandInputFile;
    resize(800, 600);

    m_controller = new Controller(this);
    createWidgets();

    connect(m_controller, &Controller::fileOpened, this, [this](int64_t durationUs) {
        m_playbackSlider->setRange(0, static_cast<int>(durationUs / 1000));
        m_playbackSlider->setValue(0);
        m_playbackSlider->setEnabled(true);
    });
    connect(m_controller, &Controller::fileClosed, this, [this]() {
        m_playbackSlider->setValue(0);
        m_playbackSlider->setEnabled(false);
    });
    connect(m_controller, &Controller::seekFinished, this, [this](bool succeeded) {
        (void)succeeded;
        m_seekPending = false;
    });
}

MainWindow::~MainWindow()
{
    m_controller->closeFile();
}

void MainWindow::createWidgets()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto frameCallback = [this]() {
        return m_controller->nextVideoFrame();
    };
    m_videoRenderer = new VideoRenderer(frameCallback, centralWidget);
    layout->addWidget(m_videoRenderer, 1);
    connect(m_videoRenderer, &VideoRenderer::initialized, this, &MainWindow::onVideoRendererInitialized);
    connect(m_controller, &Controller::playbackPositionChanged, this, &MainWindow::onPlaybackPositionChanged);
    
    // 进度条和音量条
    QHBoxLayout* slidersLayout = new QHBoxLayout();
    m_playbackSlider = new QSlider(Qt::Horizontal, centralWidget);
    connect(m_playbackSlider, &QSlider::sliderPressed, this, &MainWindow::onPlaybackSliderPressed);
    connect(m_playbackSlider, &QSlider::sliderReleased, this, &MainWindow::onPlaybackSliderReleased);
    m_playbackSlider->setEnabled(false);
    slidersLayout->addWidget(m_playbackSlider);
    m_volumeButton = new VolumeButton(m_controller->volume(), centralWidget);
    m_volumeButton->setMaximumSize(24, 24);
    m_volumeButton->setMinimumSize(24, 24);
    connect(m_volumeButton, &VolumeButton::volumeChanged, this, [this](double volume) {
        m_controller->setVolume(volume);
        QSignalBlocker blocker(m_volumeSlider); // 阻断音量条的信号再设置值
        m_volumeSlider->setValue(static_cast<int>(volume * 100));
        m_volumeButton->setVolume(volume);
    });
    slidersLayout->addWidget(m_volumeButton);
    m_volumeSlider = new QSlider(Qt::Horizontal, centralWidget);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(m_controller->volume() * 100);
    m_volumeSlider->setMaximumWidth(100);
    slidersLayout->addWidget(m_volumeSlider);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        float volume = static_cast<float>(value) / 100.0f;
        m_controller->setVolume(volume);
        m_volumeButton->setVolume(volume);
        if (volume == 0.0f) {
            m_volumeButton->clearPreviousVolume();
        }
    });
    slidersLayout->setContentsMargins(8, 3, 8, 8);
    slidersLayout->setSpacing(8);
    layout->addLayout(slidersLayout);

    QHBoxLayout* controlLayout = new QHBoxLayout();
    m_playOrPauseButton = new PlayOrPauseButton(centralWidget);
    m_playOrPauseButton->setMaximumSize(32, 32);
    m_playOrPauseButton->setMinimumSize(32, 32);
    connect(m_playOrPauseButton, &PlayOrPauseButton::clicked, this, [this]() {
        m_controller->pauseOrResume();
        if (m_controller->state() == Controller::State::Paused) {
            m_playOrPauseButton->setPaused(true);
        } else {
            m_playOrPauseButton->setPaused(false);
        }
    });
    controlLayout->addWidget(m_playOrPauseButton);
    m_stopButton = new StopButton(centralWidget);
    m_stopButton->setMaximumSize(32, 32);
    m_stopButton->setMinimumSize(32, 32);
    connect(m_stopButton, &StopButton::clicked, this, [this]() {
        m_controller->closeFile();
        m_videoRenderer->clear();
    });
    controlLayout->addWidget(m_stopButton);
    controlLayout->addStretch();
    m_playbackLabel = new PlaybackLabel(centralWidget);
    controlLayout->addWidget(m_playbackLabel);
    
    controlLayout->setContentsMargins(8, 3, 8, 8);
    controlLayout->setSpacing(2);
    layout->addLayout(controlLayout);
}

void MainWindow::onVideoRendererInitialized()
{
    if (!m_commandInputFile.isEmpty()) {
        m_controller->openFile(m_commandInputFile);
        m_commandInputFile = {};
    }
}

void MainWindow::onPlaybackSliderPressed()
{
    m_isPlaybackSliderPressed = true;
}

void MainWindow::onPlaybackSliderReleased()
{
    m_isPlaybackSliderPressed = false;
    if (!m_seekPending) {
        m_seekPending = true;
        m_controller->seek(m_playbackSlider->value() * 1000);
    }
}

void MainWindow::onPlaybackPositionChanged(int64_t positionUs)
{
    if (!m_isPlaybackSliderPressed && !m_seekPending) {
        m_playbackSlider->setValue(static_cast<int>(positionUs / 1000));
    }
}

} // namespace view