#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QTimer>
#include <cstdint>
#include "media/IMediaSource.h"
#include "audio_renderer/AudioRenderer.h"

namespace view {
class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(QObject* parent = nullptr);

    bool openFile(const QString& filePath);
    void closeFile();

    fh::FramePtr nextVideoFrame();
    fh::FramePtr nextAudioFrame();

    void pauseOrResume();

    enum class State {
        Stopped,
        Playing,
        Paused
    };
    State state() const;

    double volume() const { return m_volume; }
    void setVolume(double volume);

    void seek(int64_t positionUs);

signals:
    void playbackPositionChanged(int64_t positionUs);
    void fileOpened(int64_t durationUs);
    void fileClosed();
    void seekFinished(bool succeeded);

private:
    double m_volume{1.0};
    media::MediaSourcePtr m_mediaSource{nullptr};

    std::unique_ptr<AudioRenderer> m_audioRenderer{nullptr};

    struct Clock {
        mutable QMutex mutex;
        int64_t anchorWallUs{0};    // 锚点：墙上时间（微秒）
        int64_t anchorMediaUs{0};   // 锚点：媒体时间（微秒）
        int64_t pausedMediaUs{0};  // 暂停时冻结的媒体时间（微秒）
        bool paused{false};
    };
    Clock m_clock{};
};

} // namespace view