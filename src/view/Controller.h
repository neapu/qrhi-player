#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include "media/IMediaSource.h"
#include "audio_renderer/AudioRenderer.h"

namespace view {
class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(QObject* parent = nullptr);

    void openFile(const QString& filePath);
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

private:
    void createAudioRenderer(media::IMediaSource::AudioParams audioParams);

    // 读取当前媒体时间，需要先持有 m_clock.mutex
    int64_t clockUsLocked() const;
    void reanchorClockLocked(int64_t newAnchorMediaUs);

private:
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
};

} // namespace view