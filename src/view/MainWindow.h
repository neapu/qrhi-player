#pragma once
#include <QMainWindow>
#include <QSlider>
#include "Controller.h"
#include "video_renderer/VideoRenderer.h"
#include "components/VolumeButton.h"
#include "components/PlayOrPauseButton.h"
#include "components/StopButton.h"
#include "components/PlaybackLabel.h"

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& commandInputFile, QWidget* parent = nullptr);
    ~MainWindow();

private:
    void createWidgets();
    void createMenu();

private slots:
    void onVideoRendererInitialized();
    void onPlaybackSliderPressed();
    void onPlaybackSliderReleased();
    void onPlaybackPositionChanged(int64_t positionUs);
    void onOpenFileActionTriggered();

private:
    Controller* m_controller{nullptr};
    VideoRenderer* m_videoRenderer{nullptr};
    QString m_commandInputFile;

    QSlider* m_playbackSlider{nullptr};
    QSlider* m_volumeSlider{nullptr};
    VolumeButton* m_volumeButton{nullptr};

    PlayOrPauseButton* m_playOrPauseButton{nullptr};
    StopButton* m_stopButton{nullptr};
    PlaybackLabel* m_playbackLabel{nullptr};

    bool m_isPlaybackSliderPressed{false};
    bool m_seekPending{false};
};

} // namespace view