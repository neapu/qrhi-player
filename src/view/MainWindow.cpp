#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

#include "LogManager.h"
#include "QRhiVideoRenderer.h"
#include "AudioRenderer.h"

namespace {

constexpr int kButtonSize = 32;
constexpr int kIconSize = 18;
constexpr int kVolumeSliderWidth = 110;
constexpr int kUiRefreshIntervalMs = 100;
constexpr int kSeekDebounceMs = 150;
// seek请求由解封装线程异步处理，期间时钟仍指向旧位置；这段宽限期内不刷新UI，避免进度条回跳
constexpr int kSeekGraceMs = 500;

// images下的SVG用currentColor描述颜色，而Qt的SVG渲染器不会按调色板解析currentColor
// （恒为黑色），深色主题下图标会看不见。这里按给定颜色重新着色。
QIcon tintedIcon(const QString& path, const QColor& color)
{
    const QPixmap source = QIcon(path).pixmap(kIconSize * 2, kIconSize * 2);
    QPixmap tinted(source.size());
    tinted.setDevicePixelRatio(source.devicePixelRatio());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(QPoint(0, 0), source);
    // SourceIn：只保留绘制到的不透明像素，用目标色替换原色
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRectF(QPointF(0, 0), source.deviceIndependentSize()), color);
    return QIcon(tinted);
}

QPushButton* createIconButton(const QIcon& icon, const QString& toolTip, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus); // 播放控件不需要键盘焦点，避免点击后残留虚线框
    return button;
}

} // namespace

namespace view {

MainWindow::MainWindow(const QString& videoFile, QWidget* parent)
    : QMainWindow(parent)
    , m_videoRenderer(new QRhiVideoRenderer(this))
{
    setupMenuBar();
    setupUi();
    setupConnections();
    resize(1280, 720);

    if (!videoFile.isEmpty()) {
        openVideo(videoFile);
    }
}

MainWindow::~MainWindow()
{
    // 顺序：先停音频回调线程，再销毁渲染控件（断开get-frame回调），最后销毁controller。
    // m_controller声明在最前，成员按逆序析构，必然最后销毁
    m_uiTimer.stop();
    m_seekTimer.stop();
    m_audioRenderer.reset();
    delete m_videoRenderer;
    m_videoRenderer = nullptr;
}

void MainWindow::setupMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));

    QAction* openAction = fileMenu->addAction(tr("打开文件..."));
    openAction->setShortcut(QKeySequence::Open);
    // openAction的触发方是当前窗口，用this作为context，窗口销毁时连接自动断开
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    // 关闭唯一顶层窗口后QApplication自动退出（quitOnLastWindowClosed默认为true）
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);
}

void MainWindow::setupUi()
{
    const QColor iconColor = palette().color(QPalette::ButtonText);
    m_playIcon = tintedIcon(":/icons/play.svg", iconColor);
    m_pauseIcon = tintedIcon(":/icons/pause.svg", iconColor);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(m_videoRenderer, 1); // 视频区域占据除底部控件外的全部空间

    // 第一排（渲染组件下方）：进度条 + 音量图标 + 音量条
    auto* progressRow = new QHBoxLayout;
    progressRow->setContentsMargins(12, 8, 12, 4);
    progressRow->setSpacing(8);

    m_playbackSlider = new QSlider(Qt::Horizontal, central);
    m_playbackSlider->setRange(0, 0);
    m_playbackSlider->setEnabled(false); // 未打开媒体时不可拖动
    m_playbackSlider->setToolTip(tr("播放进度"));
    progressRow->addWidget(m_playbackSlider, 1); // 进度条吃掉本排剩余宽度

    auto* volumeIconLabel = new QLabel(central);
    volumeIconLabel->setPixmap(tintedIcon(":/icons/volume-up.svg", palette().color(QPalette::WindowText))
                                   .pixmap(QSize(16, 16)));
    progressRow->addWidget(volumeIconLabel);

    m_audioSlider = new QSlider(Qt::Horizontal, central);
    m_audioSlider->setRange(0, 100);
    m_audioSlider->setValue(static_cast<int>(m_volume * 100));
    m_audioSlider->setFixedWidth(kVolumeSliderWidth);
    m_audioSlider->setToolTip(tr("音量"));
    progressRow->addWidget(m_audioSlider);

    rootLayout->addLayout(progressRow);

    // 第二排（最下方）：按钮靠左，播放时间靠右
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(12, 0, 12, 10);
    buttonRow->setSpacing(8);

    m_pauseOrResumeButton = createIconButton(m_pauseIcon, tr("暂停/继续"), central);
    m_stopButton = createIconButton(tintedIcon(":/icons/stop.svg", iconColor), tr("停止"), central);
    buttonRow->addWidget(m_pauseOrResumeButton);
    buttonRow->addWidget(m_stopButton);
    buttonRow->addStretch(1); // 两个按钮靠左，时间标签被推到最右

    m_playbackTimeLabel = new QLabel(central);
    m_playbackTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_playbackTimeLabel->setMinimumWidth(110); // 定宽：数字变化时按钮不会左右跳动
    buttonRow->addWidget(m_playbackTimeLabel);

    rootLayout->addLayout(buttonRow);
    setCentralWidget(central);

    updatePlaybackTimeLabel(0);
}

void MainWindow::setupConnections()
{
    connect(m_pauseOrResumeButton, &QPushButton::clicked, this, &MainWindow::onPauseOrResume);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);

    connect(m_audioSlider, &QSlider::valueChanged, this, [this](int value) {
        m_volume = static_cast<float>(value) / 100.0f;
        if (m_audioRenderer) {
            m_audioRenderer->setVolume(m_volume);
        }
    });

    connect(m_playbackSlider, &QSlider::valueChanged, this, [this](int valueMs) {
        if (m_updatingPlaybackSlider) {
            return; // 定时器刷新位置，不是用户操作
        }
        m_userSeeking = true;
        updatePlaybackTimeLabel(valueMs);
        m_seekTimer.start(); // 拖动中会连续触发，防抖后只下发一次seek
    });
    connect(m_playbackSlider, &QSlider::sliderReleased, this, [this] {
        if (m_seekTimer.isActive()) {
            m_seekTimer.stop();
            commitSeek(); // 松手立即生效，无需等防抖
        }
    });

    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(kSeekDebounceMs);
    connect(&m_seekTimer, &QTimer::timeout, this, &MainWindow::commitSeek);

    m_uiTimer.setInterval(kUiRefreshIntervalMs);
    connect(&m_uiTimer, &QTimer::timeout, this, &MainWindow::refreshPlaybackUi);
    m_uiTimer.start();
}

void MainWindow::onPauseOrResume()
{
    if (!m_controller) {
        return;
    }
    m_controller->pauseOrResume();
    const bool paused = m_controller->isPaused();
    // 图标表示"点击后会发生什么"：播放中显示暂停图标，暂停中显示播放图标
    m_pauseOrResumeButton->setIcon(paused ? m_playIcon : m_pauseIcon);
    if (m_audioRenderer) {
        // 暂停时一并停掉音频设备回调：否则静音填充仍在消费音频帧，设备位置上报也会推着时钟走
        if (paused) {
            m_audioRenderer->pause();
        } else {
            m_audioRenderer->resume();
        }
    }
    refreshPlaybackUi(); // 立即刷新一次，不必等下一个定时器周期
}

void MainWindow::onStop()
{
    if (!m_controller) {
        return;
    }
    if (!m_controller->isPaused()) {
        onPauseOrResume(); // 复用暂停逻辑：音频设备、按钮图标一起复位
    }
    requestSeek(0.0); // 回到起点（保持暂停，画面停在当前帧）
    m_userSeeking = false;
    setPlaybackSliderValue(0);
    updatePlaybackTimeLabel(0);
}

void MainWindow::commitSeek()
{
    m_userSeeking = false;
    requestSeek(m_playbackSlider->value() / 1000.0);
}

void MainWindow::requestSeek(double seconds)
{
    if (!m_controller) {
        return;
    }
    m_seekGrace.start();
    m_controller->seek(seconds);
}

void MainWindow::refreshPlaybackUi()
{
    if (!m_controller || m_userSeeking) {
        return; // 用户正拖动进度条：位置以拖动的值为准
    }
    if (m_seekGrace.isValid() && m_seekGrace.elapsed() < kSeekGraceMs) {
        return; // seek尚未生效（时钟还没重锚到目标），先保留用户拖到的位置
    }
    const double positionSec = std::clamp(m_controller->position(), 0.0, m_durationSec);
    const int positionMs = static_cast<int>(positionSec * 1000.0);
    setPlaybackSliderValue(positionMs);
    updatePlaybackTimeLabel(positionMs);
}

void MainWindow::setPlaybackSliderValue(int valueMs)
{
    m_updatingPlaybackSlider = true;
    m_playbackSlider->setValue(valueMs);
    m_updatingPlaybackSlider = false;
}

void MainWindow::updatePlaybackTimeLabel(int positionMs)
{
    m_playbackTimeLabel->setText(QStringLiteral("%1 / %2")
                                     .arg(formatTime(positionMs / 1000.0), formatTime(m_durationSec)));
}

QString MainWindow::formatTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0) {
        seconds = 0;
    }
    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    const QChar pad = QLatin1Char('0');
    // 时长不足一小时时省略小时位，避免占用过多宽度
    return hours > 0 ? QStringLiteral("%1:%2:%3")
                           .arg(hours)
                           .arg(minutes, 2, 10, pad)
                           .arg(secs, 2, 10, pad)
                     : QStringLiteral("%1:%2").arg(minutes, 2, 10, pad).arg(secs, 2, 10, pad);
}

void MainWindow::onOpenFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("打开视频文件"), QString(),
        tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.ts *.m4v);;所有文件 (*)"));
    if (file.isEmpty()) {
        return; // 用户取消
    }

    // 先彻底释放上一个文件再打开新文件：避免两套解码链和音频设备同时存活
    closeVideo();
    openVideo(file);
}

void MainWindow::closeVideo()
{
    // 顺序与析构一致：音频帧回调引用controller，必须先把音频设备停掉；
    // 再停视频渲染（QRhiVideoRenderer::stop()内部会清空get-frame回调，断开对controller的引用）；
    // 最后才销毁controller，否则回调线程会访问已释放的对象
    m_audioRenderer.reset();
    m_videoRenderer->stop();
    m_controller.reset();

    // 复位播放控件：没有媒体时进度条不可用
    m_seekTimer.stop();
    m_seekGrace.invalidate();
    m_userSeeking = false;
    // serial由controller内部维护，新controller会从0重新计数，必须一起复位，否则新文件的帧会被当成过期帧丢掉
    m_maxVideoSerial = -1;
    m_durationSec = 0.0;
    m_updatingPlaybackSlider = true; // 复位不算用户拖动，避免误触发seek
    m_playbackSlider->setRange(0, 0);
    m_playbackSlider->setValue(0);
    m_updatingPlaybackSlider = false;
    m_playbackSlider->setEnabled(false);
    m_pauseOrResumeButton->setIcon(m_pauseIcon);
    updatePlaybackTimeLabel(0);
}

void MainWindow::openVideo(const QString& videoFile)
{
    using namespace controller;

    IController::Params params;
    params.url = videoFile.toStdString();
    params.logCallback = LogManager::instance().controllerLogCallback();
    m_controller = IController::create(params);
    if (!m_controller) {
        qWarning() << "Failed to open video:" << videoFile;
        return;
    }

    // 音频设备：controller初始化完即可取输出参数打开，无需等首个音频帧。
    // 设备打开失败不致命：视频回调里会排空音频队列，退化为无声播放
    if (auto audioParams = m_controller->audioParams()) {
        AudioRenderer::Params audioRendererParams;
        audioRendererParams.sampleRate = audioParams->sampleRate;
        audioRendererParams.channels = audioParams->channels;
        audioRendererParams.frameCallback = [this] { return m_controller->nextAudioFrame(); };
        audioRendererParams.playingAudioPtsCallback = [this](int64_t ptsUs) {
            m_controller->audioRenderTime(ptsUs);
        };
        m_audioRenderer = AudioRenderer::create(audioRendererParams);
        if (m_audioRenderer) {
            // 沿用用户此前拖动的音量（打开设备前可能已经调过音量条）
            m_audioRenderer->setVolume(m_volume);
        }
    }

    // 播放控件初始化：时长决定进度条范围；新文件从0开始且处于播放态
    m_durationSec = std::max(0.0, m_controller->duration());
    m_updatingPlaybackSlider = true; // 初始化范围/复位位置不算用户拖动，避免误触发seek
    m_playbackSlider->setRange(0, static_cast<int>(m_durationSec * 1000.0));
    m_playbackSlider->setValue(0);
    m_updatingPlaybackSlider = false;
    m_playbackSlider->setEnabled(m_durationSec > 0);
    m_userSeeking = false;
    m_seekGrace.invalidate();
    updatePlaybackTimeLabel(0);
    m_pauseOrResumeButton->setIcon(m_pauseIcon);

    m_videoRenderer->setGetFrameCallback([this] { return pullVideoFrame(); });
    m_videoRenderer->start();
}

controller::FramePtr MainWindow::pullVideoFrame()
{
    using namespace controller;

    // 无音频设备时音频帧无人消费，会填满队列阻塞demux线程进而拖垮视频，排空它
    if (!m_audioRenderer) {
        for (int i = 0; i < 16; ++i) {
            if (!m_controller->nextAudioFrame()) {
                break;
            }
        }
    }

    // 常规节奏每次vsync取一帧；过期serial帧就地排干（seek残留，防御性处理）
    for (int guard = 0; guard < 64; ++guard) {
        FramePtr frame = m_controller->nextVideoFrame();
        if (!frame) {
            return nullptr; // 暂停/时钟未到/队列空：保持当前画面
        }
        if (frame->type() == IFrame::FrameType::End) {
            return nullptr; // EOF：保持最后一帧画面
        }
        if (frame->serial() < m_maxVideoSerial) {
            continue;
        }
        m_maxVideoSerial = frame->serial();
        return frame;
    }
    return nullptr;
}

} // namespace view
