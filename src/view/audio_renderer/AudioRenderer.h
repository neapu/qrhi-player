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
        AVRational timeBase;
    };
    static std::unique_ptr<AudioRenderer> create(const Params& params);

    ~AudioRenderer();

    // 取值 0.0 - 1.0
    void setVolume(float volume);

    // 音频时钟（微秒）：媒体时间轴上"正在被听到"的采样时刻
    int64_t playbackPosition() const;

    // 音频时钟是否正在推进（设备在运行、已提交过数据、且未欠载）
    bool isClockAdvancing() const;

    bool start();
    void stop();

private:
    explicit AudioRenderer(const Params& params);
    
    bool initialize();

    void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

private:
    FrameCallback m_frameCallback;
    int m_sampleRate;
    int m_channels;
    AVSampleFormat m_sampleFormat;
    float m_volume;
    AVRational m_timeBase;
};

} // namespace view