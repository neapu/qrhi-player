#pragma once
#include <QElapsedTimer>
#include <QMainWindow>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <memory>
#include "controller/Controller.h"

namespace view {
class QRhiVideoRenderer;
class AudioRenderer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& videoFile, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void openVideo(const QString& videoFile);
    // 释放当前媒体：停音频设备、停视频渲染、销毁controller，并复位播放控件
    void closeVideo();
    void onOpenFile();
    // 搭建菜单栏（文件 -> 打开文件/退出）
    void setupMenuBar();
    // 搭建控件与布局；图标按调色板着色，须在控件创建前完成
    void setupUi();
    void setupConnections();

    void onPauseOrResume();
    void onStop();
    // 把进度条当前位置作为seek目标下发给controller
    void commitSeek();
    // 下发seek并开启宽限期：seek是异步的，宽限期内UI先保留目标位置
    void requestSeek(double seconds);
    // 周期刷新进度条与时间显示（用户拖动期间跳过，避免和用户抢进度条）
    void refreshPlaybackUi();
    // 写进度条时置位标记，与用户拖动区分开
    void setPlaybackSliderValue(int valueMs);
    void updatePlaybackTimeLabel(int positionMs);
    static QString formatTime(double seconds);

    // 供视频渲染器以垂直同步节奏回调：没到渲染时间或无新帧时返回nullptr
    controller::FramePtr pullVideoFrame();

private:
    controller::ControllerPtr m_controller{nullptr};
    QRhiVideoRenderer* m_videoRenderer{nullptr};
    std::unique_ptr<AudioRenderer> m_audioRenderer{nullptr};
    int m_maxVideoSerial{-1}; // 见过的最大帧serial，seek后的过期帧据此丢弃

    QSlider* m_playbackSlider{nullptr};
    QSlider* m_audioSlider{nullptr};

    QPushButton* m_pauseOrResumeButton{nullptr};
    QPushButton* m_stopButton{nullptr};

    QLabel* m_playbackTimeLabel{nullptr};

    // 按调色板着色，构造时生成一次复用
    QIcon m_playIcon;
    QIcon m_pauseIcon;

    QTimer m_uiTimer;   // 刷新进度与时间
    QTimer m_seekTimer; // 拖动进度条的防抖，避免一次拖动下发大量seek请求
    QElapsedTimer m_seekGrace; // seek下发后的宽限期，期间暂停刷新UI

    bool m_userSeeking{false};            // 用户正操作进度条，期间不被定时器覆盖
    bool m_updatingPlaybackSlider{false}; // 正在写进度条（区分定时器刷新与用户操作）
    double m_durationSec{0.0};
    float m_volume{1.0f}; // 0.0~1.0；音频设备打开前用户可能已调整过音量
};
} // namespace view
