#include "AudioRenderer.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

namespace {
// 与AV_NOPTS_VALUE一致(-2^63)，IFrame::pts()无效时的取值
constexpr int64_t NOPTS = INT64_MIN;
}

namespace view {
std::unique_ptr<AudioRenderer> AudioRenderer::create(const Params& params)
{
    if (!params.frameCallback || !params.playingAudioPtsCallback) {
        return nullptr;
    }
    if (params.channels == 0 || params.sampleRate == 0) {
        return nullptr;
    }
    auto renderer = std::unique_ptr<AudioRenderer>(new AudioRenderer(params));
    if (!renderer->start()) {
        return nullptr;
    }
    return renderer;
}

AudioRenderer::AudioRenderer(const Params& params)
    : QObject(nullptr)
    , m_params(params)
{
}

AudioRenderer::~AudioRenderer()
{
    stop();
}

bool AudioRenderer::start()
{
    if (m_device) {
        return false;
    }

    if (m_params.sampleRate <= 0 || m_params.channels <= 0) {
        return false;
    }

    // 必须用ma_device_config_init初始化：零值config的deviceType是none，
    // init/start都会返回成功但回调永远不触发，音频链路整个瘫痪
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = static_cast<ma_uint32>(m_params.channels);
    config.sampleRate = static_cast<ma_uint32>(m_params.sampleRate);
    config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* renderer = reinterpret_cast<AudioRenderer*>(pDevice->pUserData);
        renderer->maDataCallback(pDevice, pOutput, pInput, frameCount);
    };
    config.pUserData = this;

    auto dev = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, dev.get()) != MA_SUCCESS) {
        return false;
    }

    // 设备初始化后立即套用当前音量：打开设备前用户可能已经调过音量条
    ma_device_set_master_volume(dev.get(), m_volume);

    if (ma_device_start(dev.get()) != MA_SUCCESS) {
        ma_device_uninit(dev.get());
        return false;
    }

    m_device = std::move(dev);

    return true;
}

void AudioRenderer::pause()
{
    // ma_device_stop返回后回调不再触发，可安全地与回调线程并发调用
    if (m_device) {
        ma_device_stop(m_device.get());
    }
}

void AudioRenderer::resume()
{
    if (m_device) {
        ma_device_start(m_device.get());
    }
}

void AudioRenderer::stop()
{
    if (m_device) {
        ma_device_stop(m_device.get());
        ma_device_uninit(m_device.get());
        m_device.reset();
    }
}

void AudioRenderer::setVolume(float volume)
{
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    // 音量是设备内部状态，无需与回调线程同步；未打开设备时等start()里套用
    if (m_device) {
        ma_device_set_master_volume(m_device.get(), m_volume);
    }
}

void AudioRenderer::maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{
    const auto channels = pDevice->playback.channels;
    const size_t requireSize = frameCount * channels * sizeof(int16_t); // 目前固定采样格式为s16

    // 本回调开头对应的声音即将进入设备缓冲，作为发声位置回灌校准时钟；
    // 音频EOF后位置不再前进，继续上报会把时钟钉在旧位置导致视频冻结
    const int64_t pos = playbackPosUs();
    if (pos != NOPTS && !m_sawEnd) {
        m_params.playingAudioPtsCallback(pos);
    }

    size_t copyOffset = 0;
    while (copyOffset < requireSize) {
        if (!m_currentFrame) {
            m_currentFrame = m_params.frameCallback();
        }
        if (!m_currentFrame) {
            break; // 暂停/时钟未到/队列暂时为空/EOF停泊，剩余部分静音填充
        }
        if (m_currentFrame->type() == controller::IFrame::FrameType::End) {
            // EOF只投递一次，之后拉取恒为空；之后静音填充且不再上报位置
            const bool firstEnd = !m_sawEnd;
            m_sawEnd = true;
            m_currentFrame.reset();
            m_offset = 0;
            if (firstEnd) {
                // 当前处于音频线程：接收者在GUI线程时Qt自动改用队列连接投递，不会阻塞回调
                qInfo() << "Audio playback reached the end of stream.";
                emit playbackFinished();
            }
            break;
        }

        const uint8_t* data = m_currentFrame->audioData();
        const size_t frameSize = static_cast<size_t>(m_currentFrame->audioDataSize());
        if (!data || frameSize == 0 || m_offset >= frameSize) { // 防御路径，正常情况下不会走这个分支
            m_currentFrame.reset();
            m_offset = 0;
            continue;
        }

        // 当前音频帧剩余可用大小
        const size_t availableSize = frameSize - m_offset;
        const size_t copySize = std::min(requireSize - copyOffset, availableSize);
        memcpy(static_cast<uint8_t*>(pOutput) + copyOffset, data + m_offset, copySize);
        copyOffset += copySize;
        m_offset += copySize;
        if (m_offset >= frameSize) {
            if (m_currentFrame->pts() != NOPTS) {
                m_lastFrameEndUs = m_currentFrame->pts() + bytesToUs(frameSize);
            }
            m_currentFrame.reset();
            m_offset = 0;
        }
    }
    if (copyOffset < requireSize) {
        memset(static_cast<uint8_t*>(pOutput) + copyOffset, 0, requireSize - copyOffset);
    }
}

int64_t AudioRenderer::playbackPosUs() const
{
    if (m_currentFrame && m_currentFrame->pts() != NOPTS) {
        return m_currentFrame->pts() + bytesToUs(m_offset);
    }
    return m_lastFrameEndUs;
}

int64_t AudioRenderer::bytesToUs(size_t bytes) const
{
    const int64_t bytesPerSec = static_cast<int64_t>(m_params.sampleRate) * m_params.channels * sizeof(int16_t);
    return bytesPerSec > 0 ? static_cast<int64_t>(bytes) * 1'000'000 / bytesPerSec : 0;
}

} // namespace view