#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <miniaudio.h>
#include "Frame.h"

namespace view {
using MaDevicePtr = std::unique_ptr<ma_device>;
using FrameCallback = std::function<fh::FramePtr()>;
class AudioRenderer {
public:
    struct Params {
        FrameCallback frameCallback;
        int sampleRate;
        int channels;
        AVSampleFormat sampleFormat;
        float volume;
        // 音频帧 pts 的时间基（音频流一般为 1/sampleRate）；留空时按 1/sampleRate 处理
        AVRational timeBase{0, 0};
    };
    static std::unique_ptr<AudioRenderer> create(const Params& params);

    ~AudioRenderer();

    // 取值 0.0 - 1.0
    void setVolume(float volume);

    // 音频时钟（微秒）：媒体时间轴上"正在被听到"的采样时刻，已补偿设备缓冲延迟。
    // 以帧 pts 为锚点、用单调墙上时钟线性外推；欠载（无数据可播）时封顶在已提交数据的末尾。
    int64_t playbackPosition() const;

    // 是否欠载：上一次回调没能填满设备请求的数据（供不上，用静音补了）。
    // 下一次数据充足的回调会自动清除。
    bool isUnderrun() const;

    // 音频时钟是否正在推进（设备在运行、已提交过数据、且未欠载）
    bool isClockAdvancing() const;

private:
    explicit AudioRenderer(const Params& params);
    
    bool initialize();

    void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

private:
    static constexpr int64_t NO_ANCHOR = std::numeric_limits<int64_t>::min();

    FrameCallback m_frameCallback{};
    int m_sampleRate{0};
    int m_channels{0};
    AVSampleFormat m_sampleFormat{AV_SAMPLE_FMT_NONE};
    AVRational m_timeBase{0, 0};
    float m_volume{0.0};
    MaDevicePtr m_device{nullptr};

    // 仅音频线程访问
    fh::FramePtr m_currentFrame{nullptr};
    size_t m_offset{0};
    int64_t m_currentFrameStartUs{0};

    // 音频线程写、其它线程读：
    // 锚点 = 听到「当前帧首采样」的墙上时刻 - 该帧首采样的媒体时刻
    std::atomic_int64_t m_wallMinusMediaUs{NO_ANCHOR};
    // 已提交给设备的数据末尾对应的媒体时刻（欠载时用于封顶）
    std::atomic_int64_t m_submittedEndUs{0};
    // 上一次回调是否欠载
    std::atomic_bool m_underrun{false};
};

} // namespace view