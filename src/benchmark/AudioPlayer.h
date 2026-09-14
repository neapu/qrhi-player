#pragma once
#include <SDL.h>
#include "controller/Controller.h"
#include "controller/Frame.h"
#include <cstdint>
#include <chrono>
#include <mutex>

#include "Stats.h"

namespace bench {

/**
 * @brief SDL音频输出：回调线程持续拉取controller音频帧喂给声卡，
 *        并周期性回灌audioRenderTime完成时钟校准回路。
 *        音频帧必须有人消费，否则队列填满会阻塞demux线程拖垮视频。
 */
class AudioPlayer {
public:
    AudioPlayer(controller::IController& ctrl, Stats& stats);
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // 主线程调用：以controller::IController::audioParams报告的参数打开设备，
    // 首帧及后续帧由音频回调线程按需拉取
    bool start(int sampleRate, int channels);
    void stop();

private:
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
    void fill(uint8_t* stream, int len);
    // 当前正要进入设备缓冲的媒体位置
    int64_t playbackPosUs() const;
    int64_t bytesToUs(size_t bytes) const;

    controller::IController& m_ctrl;
    Stats& m_stats;
    SDL_AudioDeviceID m_device = 0;
    int m_bytesPerSec = 0; // 采样率 x 声道数 x 2字节(S16)

    // 回调线程与start/stop共享的状态
    std::mutex m_mutex;
    controller::FramePtr m_current; // 正在喂给声卡的帧
    size_t m_offsetBytes = 0;       // 当前帧内已消费的字节偏移
    int64_t m_lastFrameEndUs = kNoPts; // 最近耗尽帧的结束位置（underrun期间的上报值）
    int m_maxSerial = -1;           // 见过的最大serial，seek后过期帧据此丢弃
    bool m_sawEnd = false;          // 音频EOF：静音填充且不再上报位置
    bool m_gotFirstFrame = false;   // 起播宽容期：首帧到达前的静音不算underrun
    // 饥饿检测：短暂断流逐回调计underrun，持续超过2s记一次stall不再刷计数
    std::chrono::steady_clock::time_point m_starveStart{};
    bool m_stallCounted = false;
};

} // namespace bench
