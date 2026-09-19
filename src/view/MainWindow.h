#pragma once
#include <QMainWindow>
#include <QSlider>
#include <QBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include "video_renderer/QRhiVideoRenderer.h"
#include "audio_renderer/AudioRenderer.h"
#include "controller/Controller.h"
#include "Stats.h"

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &commandInputFile, QWidget *parent = nullptr);
    ~MainWindow();

private:
    void openVideo(const QString &videoFile);
    void closeVideo();

    void createMenuBar();
    void createCentralWidget();
    void createControlWidgets(QBoxLayout* layout, QWidget* parent);

    controller::FramePtr getVideoFrame();

    enum class State {
        Playing,
        Paused,
        Stopped
    };
    void setState(State state);

    // 根据当前音量切换音量图标（静音 / 低 / 高）并更新提示文字
    void updateVolumeIcon();

private slots:
    void openFile();
    void onVideoRendererError(const QString &error);
    void onVolumeIconClicked();
    void onPlayOrPauseClicked();
    void onStopClicked();
    void onPlaybackSliderPressed();
    void onPlaybackSliderReleased();
    void onPlayback(int64_t ptsUs);
    void onPlaybackFinished();

private:
    view::QRhiVideoRenderer* m_videoRenderer{};
    std::unique_ptr<view::AudioRenderer> m_audioRenderer{};
    controller::ControllerPtr m_controller{};
    int m_serial{-1};
    float m_volume{1.0f};

    QSlider* m_playbackSlider{};
    QSlider* m_volumeSlider{};
    QToolButton* m_volumeIconButton{};
    QIcon m_volumeMuteIcon{};
    QIcon m_volumeDownIcon{};
    QIcon m_volumeUpIcon{};
    // 静音前的音量，点击图标取消静音时用来恢复
    float m_volumeBeforeMute{1.0f};
    
    QPushButton* m_playOrPauseButton{};
    QIcon m_playIcon{};
    QIcon m_pauseIcon{};
    QIcon m_stopIcon{};

    QPushButton* m_stopButton{};
    QLabel* m_playbackLabel{};
    
    State m_state{State::Stopped};

    bool m_isPlaybackSliderPressed{false};
    QString m_durationText{};

    bool m_videoEndFlag{false};
    bool m_audioEndFlag{false};

    QString m_videoFileName{};
    view::Stats* m_stats{};
};

} // namespace view