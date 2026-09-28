#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
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
    void createAudioRenderer(media::IMediaSource::AudioParams audioParams);

    // 读取当前媒体时间，需要先持有 m_clock.mutex
    int64_t clockUsLocked() const;
    void reanchorClockLocked(int64_t newAnchorMediaUs);
    void handleSeekCompleted(bool succeeded, int serial, uint64_t mediaGeneration);
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

    fh::FramePtr m_pendingAudioFrame{nullptr};
    fh::FramePtr m_pendingVideoFrame{nullptr};

    bool m_hasVideo{false};
    std::atomic_bool m_hasAudio{false};

    int64_t m_videoFrameDurationUs{0};
    AVRational m_videoTimeBase{0, 1};
    AVRational m_audioTimeBase{0, 1};

    std::atomic_int m_serial{0};
    int64_t m_pendingSeekPositionUs{0};
    uint64_t m_mediaGeneration{0};

    bool m_videoEnd{false};
    bool m_audioEnd{false};
};

} // namespace view