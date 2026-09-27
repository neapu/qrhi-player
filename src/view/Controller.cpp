#include "Controller.h"
#include "LogManager.h"
#include <QDebug>

namespace {
int64_t steadyClockUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
int64_t frameDurationUs(AVRational frameRate)
{
    if (frameRate.num == 0) {
        return 0;
    }
    return static_cast<int64_t>(1'000'000 * frameRate.den / static_cast<double>(frameRate.num));
}

int64_t framePtsUs(const fh::FramePtr& frame, AVRational timeBase)
{
    if (!frame) {
        return 0;
    }
    auto tb = frame->time_base.num != 0 ? frame->time_base : timeBase;
    int64_t pts = frame->pts;
    if (pts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
    return av_rescale_q(pts, tb, AVRational{1, 1'000'000});
}

int getFrameSerial(const fh::FramePtr& frame)
{
    if (!frame) {
        return -1;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(frame->opaque));
}

}

namespace view {
Controller::Controller(QObject* parent)
    : QObject(parent)
{
}

void Controller::createAudioRenderer(media::IMediaSource::AudioParams audioParams)
{
    m_audioTimeBase = audioParams.timeBase;
    AudioRenderer::Params params{};
    params.sampleRate = audioParams.sampleRate;
    params.channels = audioParams.channels;
    params.sampleFormat = audioParams.sampleFormat;
    params.timeBase = audioParams.timeBase;
    params.frameCallback = [this]() -> fh::FramePtr {
        return nextAudioFrame();
    };
    params.volume = m_volume;
    m_audioRenderer = AudioRenderer::create(params);
    if (!m_audioRenderer) {
        qCritical() << "Failed to create audio renderer";
        m_hasAudio = false;
        m_audioEnd = true;
    }
}

int64_t Controller::clockUsLocked() const
{
    return m_clock.anchorMediaUs + (steadyClockUs() - m_clock.anchorWallUs);
}

void Controller::reanchorClockLocked(int64_t newAnchorMediaUs)
{
    m_clock.anchorMediaUs = newAnchorMediaUs;
    m_clock.anchorWallUs = steadyClockUs();
}

void Controller::openFile(const QString& filePath)
{
    closeFile();
    const uint64_t mediaGeneration = m_mediaGeneration;
    media::IMediaSource::Params params{};
    params.source = filePath.toStdString();
    params.logDir = view::LogManager::instance().logDir().toStdString();
    params.requiredPixelFormats = {AV_PIX_FMT_YUV420P};
    params.requiredSampleFormats = {AV_SAMPLE_FMT_FLT};
    params.initialSerial = m_serial;
    params.onSeekCompleted = [this, mediaGeneration](bool succeeded, int serial) {
        QMetaObject::invokeMethod(this, [this, succeeded, serial, mediaGeneration]() {
            handleSeekCompleted(succeeded, serial, mediaGeneration);
        }, Qt::QueuedConnection);
    };
    m_mediaSource = media::IMediaSource::create(params);
    if (!m_mediaSource) {
        qCritical() << "Failed to open media source:" << filePath;
        return;
    }

    {
        QMutexLocker locker(&m_clock.mutex);
        m_clock.anchorWallUs = steadyClockUs();
        m_clock.anchorMediaUs = 0;
        m_clock.pausedMediaUs = 0;
        m_clock.paused = false;
    }

    // 先创建渲染器再启动管线：音频设备是否可用决定 MediaSource 选用哪条流作为主流
    auto audioParams = m_mediaSource->audioParams();
    m_hasAudio = audioParams.has_value();
    m_audioEnd = !m_hasAudio;
    if (audioParams) {
        createAudioRenderer(*audioParams);
    }

    // 根据帧率估算一帧视频帧的持续时间
    auto videoParams = m_mediaSource->videoParams();
    m_hasVideo = videoParams.has_value();
    m_videoEnd = !m_hasVideo;
    if (videoParams) {
        m_videoFrameDurationUs = frameDurationUs(videoParams->frameRate);
        if (m_videoFrameDurationUs == 0) {
            // 未知帧率时，兜底按照10fps算
            m_videoFrameDurationUs = 1000000 / 10;
        }
        m_videoFrameDurationUs;
        m_videoTimeBase = videoParams->timeBase;
    }

    // 纯音频文件且音频设备不可用，没有任何流可以消费
    if (!m_hasVideo && !m_hasAudio) {
        qCritical() << "Audio-only media but audio device unavailable:" << filePath;
        closeFile();
        return;
    }

    emit fileOpened(m_mediaSource->duration());
    m_mediaSource->start(m_hasAudio);
}

void Controller::closeFile()
{
    ++m_mediaGeneration;
    m_audioRenderer.reset();
    m_audioTimeBase = AVRational{0, 1};
    m_hasAudio = false;
    m_videoFrameDurationUs = 0;
    m_videoTimeBase = AVRational{0, 1};
    m_hasVideo = false;
    m_pendingVideoFrame.reset();
    m_pendingAudioFrame.reset();
    m_mediaSource.reset();
    {
        QMutexLocker locker(&m_clock.mutex);
        m_clock.anchorWallUs = 0;
        m_clock.anchorMediaUs = 0;
        m_clock.pausedMediaUs = 0;
        m_clock.paused = false;
    }
    emit fileClosed();
}

fh::FramePtr Controller::nextVideoFrame()
{
    // QRhi的render也在GUI线程，不会与closeFile冲突
    if (!m_hasVideo) {
        return nullptr;
    }
    for (;;) {
        if (!m_mediaSource) {
            return nullptr;
        }
        if (!m_pendingVideoFrame) {
            media::MediaFrame mediaFrame = m_mediaSource->nextVideoFrame();
            std::visit([this](auto&& frame) {
                if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, fh::FramePtr>) {
                    m_pendingVideoFrame = std::move(frame);
                } else if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, media::EndFrame>) {
                    m_videoEnd = true;
                    if (m_videoEnd && m_audioEnd) {
                        closeFile();
                    }
                } else if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, media::EmptyFrame>) {
                    qDebug() << "Empty frame received";
                    m_pendingVideoFrame = nullptr;
                } else {
                    qCritical() << "Unknown frame type received";
                }
            }, std::move(mediaFrame));
            
        }
        if (!m_pendingVideoFrame) {
            return nullptr;
        }

        int serial = getFrameSerial(m_pendingVideoFrame);
        if (serial < m_serial) {
            m_pendingVideoFrame.reset();
            continue;
        }
        m_serial = serial;
        // 取媒体时间
        QMutexLocker locker(&m_clock.mutex);
        if (m_clock.paused) {
            return nullptr;
        }
        int64_t mediaUs = clockUsLocked();
        if (m_audioRenderer && m_audioRenderer->isClockAdvancing()) {
            mediaUs = m_audioRenderer->playbackPosition();
        }
        reanchorClockLocked(mediaUs);
        // 如果pts早于媒体时间超过持续时间，这一帧需要丢弃
        int64_t ptsUs = framePtsUs(m_pendingVideoFrame, m_videoTimeBase);
        if (ptsUs == AV_NOPTS_VALUE) {
            // TODO: 针对无PTS视频帧，可能需要根据帧率或其他信息生成PTS，以后再处理
            return std::exchange(m_pendingVideoFrame, nullptr);
        }
        auto frameDurationUs = m_videoFrameDurationUs + 2000; // 加一点容错
        if (ptsUs + frameDurationUs < mediaUs) {
            m_pendingVideoFrame.reset();
            continue;
        }
        // 如果pts晚于媒体时间，还没到播放时间，返回nullptr
        if (ptsUs > mediaUs) {
            return nullptr;
        }

        if (!m_hasAudio) {
            emit playbackPositionChanged(mediaUs);
        }
        return std::exchange(m_pendingVideoFrame, nullptr);
    }
    return nullptr;
}

fh::FramePtr Controller::nextAudioFrame()
{
    if (!m_hasAudio) {
        return nullptr;
    }
    for (;;) {
        if (!m_mediaSource) {
            return nullptr;
        }
        if (!m_pendingAudioFrame) {
            media::MediaFrame mediaFrame = m_mediaSource->nextAudioFrame();
            std::visit([this](auto&& frame) {
                if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, fh::FramePtr>) {
                    m_pendingAudioFrame = std::move(frame);
                } else if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, media::EndFrame>) {
                    // 这个回调在音频线程，需要切换到gui线程
                    QMetaObject::invokeMethod(this, [this]() {
                        m_audioEnd = true;
                        if (m_videoEnd && m_audioEnd) {
                            closeFile();
                        }
                    }, Qt::QueuedConnection);
                } else if constexpr (std::is_same_v<std::decay_t<decltype(frame)>, media::EmptyFrame>) {
                    qDebug() << "Empty frame received";
                    m_pendingAudioFrame.reset();
                } else {
                    qCritical() << "Unknown frame type received";
                }
            }, std::move(mediaFrame));
        }
        if (!m_pendingAudioFrame) {
            return nullptr;
        }

        int serial = getFrameSerial(m_pendingAudioFrame);
        if (serial < m_serial) {
            m_pendingAudioFrame.reset();
            continue;
        }
        m_serial = serial;
        // 取媒体时间
        QMutexLocker locker(&m_clock.mutex);
        if (m_clock.paused) {
            return nullptr;
        }
        int64_t mediaUs = clockUsLocked();
        if (m_audioRenderer && m_audioRenderer->isClockAdvancing()) {
            mediaUs = m_audioRenderer->playbackPosition();
        }
        reanchorClockLocked(mediaUs);

        // 如果pts早于媒体时间超过一帧的持续时间，这一帧需要丢弃
        int64_t ptsUs = framePtsUs(m_pendingAudioFrame, m_audioTimeBase);
        // 持续时间 = 包含的样本数 / 采样率
        int64_t frameDurationUs = static_cast<int64_t>(
            static_cast<double>(m_pendingAudioFrame->nb_samples) /
            m_pendingAudioFrame->sample_rate * 1000000);
        frameDurationUs += 2000; // 加一点容错
        if (ptsUs + frameDurationUs < mediaUs) {
            m_pendingAudioFrame.reset();
            continue;
        }

        emit playbackPositionChanged(mediaUs);
        return std::exchange(m_pendingAudioFrame, nullptr);
    }
    return nullptr;
}

void Controller::pauseOrResume()
{
    QMutexLocker locker(&m_clock.mutex);
    if (m_clock.paused) {
        m_clock.paused = false;
        reanchorClockLocked(m_clock.pausedMediaUs);
    } else {
        m_clock.paused = true;
        m_clock.pausedMediaUs = clockUsLocked();
    }
}

Controller::State Controller::state() const
{
    if (!m_mediaSource) {
        return State::Stopped;
    }
    QMutexLocker locker(&m_clock.mutex);
    if (m_clock.paused) {
        return State::Paused;
    }
    return State::Playing;
}

void Controller::setVolume(double volume)
{
    m_volume = volume;
    if (m_audioRenderer) {
        m_audioRenderer->setVolume(m_volume);
    }
}

void Controller::handleSeekCompleted(bool succeeded, int serial, uint64_t mediaGeneration)
{
    if (mediaGeneration != m_mediaGeneration) {
        return;
    }
    if (!succeeded) {
        emit seekFinished(false);
        return;
    }

    m_videoEnd = !m_hasVideo;
    m_audioEnd = !m_hasAudio;
    m_serial = serial;
    {
        QMutexLocker locker(&m_clock.mutex);
        reanchorClockLocked(m_pendingSeekPositionUs);
    }
    emit seekFinished(true);
}

void Controller::seek(int64_t positionUs)
{
    // MediaSource 的设计是播放完后可以继续 seek 到任意位置
    // Controller 的设计是播放完后就关闭 m_mediaSource, 状态回到 Stopped
    if (!m_mediaSource) {
        emit seekFinished(false);
        return;
    }
    m_pendingSeekPositionUs = positionUs;
    m_mediaSource->seek(positionUs);
}

} // namespace view