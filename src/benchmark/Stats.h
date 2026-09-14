#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bench {

// demo内部的时间戳哨兵，语义同AV_NOPTS_VALUE
inline constexpr int64_t kNoPts = INT64_MIN;

/**
 * @brief 基准指标收集。视频侧方法只在主线程调用；
 *        音频侧方法由SDL音频回调线程调用，内部用原子量保证安全
 */
class Stats {
public:
    using Clock = std::chrono::steady_clock;

    struct SeekRecord {
        double targetSec = 0.0;
        int serialAtRequest = -1;   // 发起seek时已显示的最大serial
        Clock::time_point requestedAt{};
        bool completed = false;     // 新serial首帧已显示
        int64_t latencyUs = -1;     // seek()调用 -> 新serial首帧显示
    };

    // ---- 视频侧（主线程） ----
    void onLoop(Clock::time_point now, bool paused);
    void onVideoFrame(int64_t ptsUs, int serial, Clock::time_point now);
    void onNullPull() { ++m_nullPulls; }
    void onStaleFrameDiscarded() { ++m_staleDiscarded; }
    void onVideoEnd() { ++m_videoEndCount; }
    void onSeekRequested(double targetSec, int serialAtRequest, Clock::time_point now);
    void onAudioStarted(int sampleRate, int channels);

    // ---- 音频侧（SDL回调线程） ----
    void onAudioFrame(int64_t ptsUs, int samples);
    void onAudioUnderrun() { ++m_audioUnderruns; }
    void onAudioStall() { ++m_audioStalls; }
    void onAudioEnd() { ++m_audioEndCount; }
    void onStaleAudioDiscarded() { ++m_staleAudioDiscarded; }

    // ---- 输出 ----
    std::string liveLine(double durationSec, bool paused, bool eof) const;
    void printSummary(double durationSec) const;

private:
    int64_t medianFrameDeltaUs() const;

    // 视频侧状态
    Clock::time_point m_runStart{ Clock::now() };
    uint64_t m_displayed = 0;
    uint64_t m_nullPulls = 0;
    uint64_t m_staleDiscarded = 0;
    uint64_t m_droppedEstimate = 0;
    uint32_t m_videoEndCount = 0;
    int64_t m_lastShownPts = 0;

    // pts呈现误差 = wall流逝 - pts流逝，serial切换(时钟重锚)时重新累积
    bool m_anchored = false;
    int m_anchorSerial = -1;
    Clock::time_point m_anchorWall{};
    int64_t m_anchorPts = 0;
    uint64_t m_errN = 0;
    double m_errSumUs = 0.0;
    double m_errSumSqUs = 0.0;
    double m_errMaxAbsUs = 0.0;

    // 丢帧估算：以显示帧pts间隔的中位数作为基准节拍
    int64_t m_lastPts = kNoPts;
    int m_lastSerial = -1;
    std::deque<int64_t> m_deltas;

    // 实时fps：最近1s内的显示时间戳
    std::deque<Clock::time_point> m_displayTimes;

    // 暂停累计
    double m_pausedTotalSec = 0.0;
    Clock::time_point m_lastLoop{};

    int m_audioSampleRate = 0;
    int m_audioChannels = 0;

    std::vector<SeekRecord> m_seeks;

    // 音频侧（原子）
    std::atomic<uint64_t> m_audioFrames{ 0 };
    std::atomic<uint64_t> m_audioSamples{ 0 };
    std::atomic<uint64_t> m_audioUnderruns{ 0 };
    std::atomic<uint64_t> m_audioStalls{ 0 };
    std::atomic<uint64_t> m_audioEndCount{ 0 };
    std::atomic<uint64_t> m_staleAudioDiscarded{ 0 };
};

} // namespace bench
