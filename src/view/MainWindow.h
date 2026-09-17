#pragma once
#include <QMainWindow>
#include "video_renderer/QRhiVideoRenderer.h"
#include "audio_renderer/AudioRenderer.h"
#include "controller/Controller.h"

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

    controller::FramePtr getVideoFrame();

private slots:
    void onVideoRendererError(const QString &error);

private:
    view::QRhiVideoRenderer* m_videoRenderer{};
    std::unique_ptr<view::AudioRenderer> m_audioRenderer{};
    controller::ControllerPtr m_controller{};
    int m_serial{-1};
    float m_volume{1.0f};
};

} // namespace view