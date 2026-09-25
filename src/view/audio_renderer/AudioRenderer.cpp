#include "AudioRenderer.h"
#include <QDebug>
#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavutil/mathematics.h>
}

namespace {
constexpr int64_t US_PER_SECOND = 1000000;

// 经验值：设备"写入 -> 被听到"的延迟。WASAPI 共享模式默认 period 10ms × 2 periods ≈ 20ms。
constexpr int64_t LATENCY_COMPENSATION_US = 20 * 1000;

// 单调墙上时钟（微秒）
int64_t steadyClockUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 采样数 -> 微秒
int64_t framesToUs(int64_t frames, int sampleRate)
{
    return sampleRate > 0 ? frames * US_PER_SECOND / sampleRate : 0;
}

// 帧时间戳（timeBase 单位）-> 微秒
int64_t timestampToUs(int64_t timestamp, AVRational timeBase)
{
    return av_rescale_q(timestamp, timeBase, AV_TIME_BASE_Q);
}

// miniaudio 只支持交错格式（不支持 deinterleaved），这里也只接受交错格式，
// 平面格式（*P）由 MediaSource 侧转换后再送进来。
ma_format toMaFormat(AVSampleFormat sampleFormat)
{
    switch (sampleFormat) {
        case AV_SAMPLE_FMT_U8:  return ma_format_u8;
        case AV_SAMPLE_FMT_S16: return ma_format_s16;
        case AV_SAMPLE_FMT_S32: return ma_format_s32;
        case AV_SAMPLE_FMT_FLT: return ma_format_f32;
        default: return ma_format_unknown;
    }
}

}

namespace view {
std::unique_ptr<AudioRenderer> AudioRenderer::create(const Params& params)
{
    auto renderer = std::unique_ptr<AudioRenderer>(new AudioRenderer(params));
    if (renderer->initialize()) {
        return renderer;
    }
    return nullptr;
}

AudioRenderer::AudioRenderer(const Params& params)
    : m_frameCallback(params.frameCallback),
      m_sampleRate(params.sampleRate),
      m_channels(params.channels),
      m_sampleFormat(params.sampleFormat),
      m_timeBase(params.timeBase),
      m_volume(params.volume)
{
    m_volume = std::clamp(m_volume, 0.0f, 1.0f);
    if (m_timeBase.num <= 0 || m_timeBase.den <= 0) {
        // 音频流的 pts 时间基几乎总是采样周期
        m_timeBase = av_make_q(1, m_sampleRate > 0 ? m_sampleRate : 1);
    }
}

AudioRenderer::~AudioRenderer()
{
    if (m_device) {
        if (ma_device_is_started(m_device.get())) ma_device_stop(m_device.get());
        ma_device_uninit(m_device.get());
    }
}

bool AudioRenderer::initialize()
{
    if (!m_frameCallback) {
        qCritical() << "Frame callback is not set.";
        return false;
    }

    if (m_sampleRate <= 0 || m_channels <= 0 || m_sampleFormat == AV_SAMPLE_FMT_NONE) {
        qCritical() << "Invalid audio parameters.";
        return false;
    }

    auto maFormat = toMaFormat(m_sampleFormat);
    if (maFormat == ma_format_unknown) {
        qCritical() << "Unsupported audio format: " << static_cast<int>(m_sampleFormat);
        return false;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = maFormat;
    config.playback.channels = static_cast<ma_uint32>(m_channels);
    config.sampleRate = static_cast<ma_uint32>(m_sampleRate);
    config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* renderer = reinterpret_cast<AudioRenderer*>(pDevice->pUserData);
        renderer->maDataCallback(pDevice, pOutput, pInput, frameCount);
    };
    config.pUserData = this;

    auto dev = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, dev.get()) != MA_SUCCESS) {
        qCritical() << "Failed to initialize audio device.";
        return false;
    }
    ma_device_set_master_volume(dev.get(), m_volume);

    if (ma_device_start(dev.get()) != MA_SUCCESS) {
        qCritical() << "Failed to start audio device.";
        ma_device_uninit(dev.get());
        return false;
    }
    m_device = std::move(dev);

    return true;
}

void AudioRenderer::setVolume(float volume)
{
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_device) {
        ma_device_set_master_volume(m_device.get(), m_volume);
    }
}

int64_t AudioRenderer::playbackPosition() const
{
    const int64_t wallMinusMediaUs = m_wallMinusMediaUs.load(std::memory_order_acquire);
    if (wallMinusMediaUs == NO_ANCHOR) {
        return 0;   // 还没有提交任何音频数据
    }

    // 锚点线性外推：设备按实时速率消费数据，墙上时钟走多少，媒体时间就走多少
    const int64_t heardUs = std::max<int64_t>(steadyClockUs() - wallMinusMediaUs, 0);

    // 欠载（无数据可提交、设备在放静音）时，位置不能越过已提交数据的末尾
    return std::min(heardUs, m_submittedEndUs.load(std::memory_order_acquire));
}

bool AudioRenderer::isUnderrun() const
{
    return m_underrun.load(std::memory_order_relaxed);
}

bool AudioRenderer::isClockAdvancing() const
{
    // 设备停止、还没提交过数据、或正在欠载（时钟被封顶止步）都不算推进
    return m_device && ma_device_is_started(m_device.get())
        && m_wallMinusMediaUs.load(std::memory_order_relaxed) != NO_ANCHOR
        && !m_underrun.load(std::memory_order_relaxed);
}

void AudioRenderer::maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{
    (void)pDevice;
    (void)pInput;

    // 只处理交错格式（平面格式由 MediaSource 转换）：一帧的数据是连续的，整段拷贝即可
    const size_t frameBytes = static_cast<size_t>(m_channels)
        * static_cast<size_t>(av_get_bytes_per_sample(m_sampleFormat));
    auto* out = static_cast<uint8_t*>(pOutput);

    const int64_t nowUs = steadyClockUs();
    uint32_t written = 0;
    while (written < frameCount) {
        if (!m_currentFrame) {
            m_currentFrame = m_frameCallback ? m_frameCallback() : nullptr;
            m_offset = 0;
            if (!m_currentFrame) break;
            if (m_currentFrame->nb_samples <= 0) {
                m_currentFrame.reset();
                break;
            }
            // 本帧首采样的媒体时刻：优先用 pts；缺失时按上一帧末尾推算（音频数据是连续的）
            const int64_t pts = m_currentFrame->best_effort_timestamp;
            m_currentFrameStartUs = (pts != AV_NOPTS_VALUE)
                ? timestampToUs(pts, m_timeBase)
                : m_submittedEndUs.load(std::memory_order_relaxed);
        }

        const auto available = static_cast<size_t>(m_currentFrame->nb_samples);
        if (m_offset >= available) {
            m_currentFrame.reset();
            m_offset = 0;
            continue;
        }

        const size_t startOffset = m_offset;
        const uint32_t writtenBeforeFrame = written;
        const size_t copyFrames = std::min<size_t>(frameCount - written, available - m_offset);
        std::memcpy(out + static_cast<size_t>(written) * frameBytes,
                    m_currentFrame->data[0] + m_offset * frameBytes,
                    copyFrames * frameBytes);

        m_offset += copyFrames;
        written += static_cast<uint32_t>(copyFrames);

        if (startOffset == 0) {
            // 本帧首采样刚被写到设备写指针处：它将在"现在 + 设备延迟 + 本回调内此前的时长"后被听到
            const int64_t heardWallUs = nowUs + LATENCY_COMPENSATION_US + framesToUs(writtenBeforeFrame, m_sampleRate);
            m_wallMinusMediaUs.store(heardWallUs - m_currentFrameStartUs, std::memory_order_release);
        }
        // 已提交数据的末尾：欠载时用于给播放位置封顶
        m_submittedEndUs.store(m_currentFrameStartUs + framesToUs(static_cast<int64_t>(m_offset), m_sampleRate),
                               std::memory_order_relaxed);
    }

    // 数据不足时补静音
    if (written < frameCount) {
        std::memset(out + static_cast<size_t>(written) * frameBytes,
                    0,
                    static_cast<size_t>(frameCount - written) * frameBytes);
    }

    // 没填满即欠载（数据供不上），下一次填满时自动清除
    m_underrun.store(written < frameCount, std::memory_order_relaxed);
}

} // namespace view