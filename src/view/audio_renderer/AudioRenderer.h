#pragma once
#include <memory>
#include <functional>
#include <cstdint>
#include <miniaudio.h>
#include "controller/Frame.h"

namespace view {
using FrameCallback = std::function<controller::FramePtr()>;
using PlayingAudioPtsCallback = std::function<void(int64_t ptsUs)>;
using MaDevicePtr = std::unique_ptr<ma_device>;

class AudioRenderer {
public:
    struct Params {
        FrameCallback frameCallback{};
        PlayingAudioPtsCallback playingAudioPtsCallback{};
        int sampleRate{};
        int channels{};
    };
    static std::unique_ptr<AudioRenderer> create(const Params& params);
    ~AudioRenderer();

    // 停止/恢复音频设备回调；暂停期间不再消费音频帧，帧队列填满后反压demux
    void pause();
    void resume();
    // 设置音量，取值0.0~1.0（越界会被clamp），设备未打开时仅记录，start()时生效
    void setVolume(float volume);
private:
    explicit AudioRenderer(const Params& params);
    bool start();
    void stop();

    void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
    // 当前正要进入设备缓冲的媒体位置(us)，无效时返回kNoPts
    int64_t playbackPosUs() const;
    int64_t bytesToUs(size_t bytes) const;

private:
    Params m_params{};

    MaDevicePtr m_device{};
    controller::FramePtr m_currentFrame{};
    size_t m_offset{}; // 当前帧内已消费的字节偏移
    int64_t m_lastFrameEndUs{INT64_MIN}; // 最近耗尽帧的结束位置(us)，underrun期间的上报值
    bool m_sawEnd{false}; // 音频EOF：静音填充且不再上报位置，否则时钟被钉在旧位置导致视频冻结
    float m_volume{1.0f}; // 线性音量，0.0~1.0
};


} // namespace view
