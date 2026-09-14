#include "AudioPlayer.h"
#include <cstring>
// 音频PCM数据只能经rawFrame()按AVFrame读取（公共接口只暴露格式元数据）
#include <libavutil/frame.h>

namespace bench {

AudioPlayer::AudioPlayer(controller::IController& ctrl, Stats& stats)
    : m_ctrl(ctrl)
    , m_stats(stats)
{
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

bool AudioPlayer::start(int sampleRate, int channels, controller::FramePtr firstFrame)
{
    if (m_device) {
        return true;
    }
    if (!firstFrame || sampleRate <= 0 || channels <= 0) {
        return false;
    }

    SDL_AudioSpec want{};
    want.freq = sampleRate;
    want.format = AUDIO_S16LSB; // 对应IFrame::SampleFormat::S16LE
    want.channels = static_cast<Uint8>(channels);
    want.samples = 1024; // ~21ms@48kHz，回调粒度即audioRenderTime的反馈粒度
    want.callback = &AudioPlayer::audioCallback;
    want.userdata = this;
    SDL_AudioSpec have{};
    // allowed_changes=0：SDL内部做格式/采样率转换，回调侧始终拿到want格式
    m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_device == 0) {
        return false;
    }
    m_bytesPerSec = sampleRate * channels * 2;
    {
        std::lock_guard lock(m_mutex);
        m_current = std::move(firstFrame);
        m_offsetBytes = 0;
        m_maxSerial = m_current ? m_current->serial() : -1;
        m_lastFrameEndUs = kNoPts;
        m_sawEnd = false;
    }
    SDL_PauseAudioDevice(m_device, 0);
    return true;
}

void AudioPlayer::stop()
{
    if (m_device == 0) {
        return;
    }
    // CloseAudioDevice会先停掉回调线程再返回
    SDL_CloseAudioDevice(m_device);
    m_device = 0;
    std::lock_guard lock(m_mutex);
    m_current.reset();
    m_offsetBytes = 0;
}

void SDLCALL AudioPlayer::audioCallback(void* userdata, Uint8* stream, int len)
{
    static_cast<AudioPlayer*>(userdata)->fill(stream, len);
}

void AudioPlayer::fill(uint8_t* stream, int len)
{
    std::lock_guard lock(m_mutex);

    // 本回调开头对应的声音即将进入设备缓冲，作为发声位置回灌校准时钟；
    // 音频EOF后位置不再前进，继续上报会把时钟钉在旧位置导致视频冻结
    const int64_t pos = playbackPosUs();
    if (pos != kNoPts && !m_sawEnd) {
        m_ctrl.audioRenderTime(pos);
    }

    size_t filled = 0;
    while (filled < static_cast<size_t>(len)) {
        if (!m_current) {
            controller::FramePtr frame = m_ctrl.nextAudioFrame();
            if (!frame) {
                // 暂停/时钟未到/队列暂时为空/EOF停泊；仅真正的饥饿才计underrun
                if (!m_ctrl.isPaused() && !m_sawEnd) {
                    const auto now = std::chrono::steady_clock::now();
                    if (m_starveStart == std::chrono::steady_clock::time_point{}) {
                        m_starveStart = now;
                    }
                    using namespace std::chrono_literals;
                    if (now - m_starveStart < 2s) {
                        m_stats.onAudioUnderrun();
                    } else if (!m_stallCounted) {
                        // 持续断流（如音频End标记未送达时的EOF停泊）只记一次事件
                        m_stallCounted = true;
                        m_stats.onAudioStall();
                    }
                }
                break;
            }
            m_starveStart = {};
            m_stallCounted = false;
            if (frame->type() == controller::IFrame::FrameType::End) {
                // EOF只投递一次，之后拉取恒为空；seek复活后会有新serial帧到来
                m_sawEnd = true;
                m_stats.onAudioEnd();
                break;
            }
            if (frame->serial() < m_maxSerial) {
                m_stats.onStaleAudioDiscarded();
                continue;
            }
            m_maxSerial = frame->serial();
            m_sawEnd = false;
            m_stats.onAudioFrame(frame->pts(), frame->samples());
            m_current = std::move(frame);
            m_offsetBytes = 0;
        }

        auto* avf = static_cast<AVFrame*>(m_current->rawFrame());
        // S16为打包格式，data[0]即交错PCM
        const size_t frameBytes = static_cast<size_t>(avf->nb_samples) * m_current->channels() * 2;
        const size_t avail = frameBytes - m_offsetBytes;
        const size_t n = std::min(avail, static_cast<size_t>(len) - filled);
        std::memcpy(stream + filled, avf->data[0] + m_offsetBytes, n);
        filled += n;
        m_offsetBytes += n;
        if (m_offsetBytes >= frameBytes) {
            if (m_current->pts() != kNoPts) {
                m_lastFrameEndUs = m_current->pts() + bytesToUs(frameBytes);
            }
            m_current.reset();
            m_offsetBytes = 0;
        }
    }
    if (filled < static_cast<size_t>(len)) {
        std::memset(stream + filled, 0, static_cast<size_t>(len) - filled);
    }
}

int64_t AudioPlayer::playbackPosUs() const
{
    if (m_current && m_current->pts() != kNoPts) {
        return m_current->pts() + bytesToUs(m_offsetBytes);
    }
    return m_lastFrameEndUs;
}

int64_t AudioPlayer::bytesToUs(size_t bytes) const
{
    return m_bytesPerSec > 0 ? static_cast<int64_t>(bytes) * 1'000'000 / m_bytesPerSec : 0;
}

} // namespace bench
