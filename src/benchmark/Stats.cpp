#include "Stats.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <print>

namespace bench {

using namespace std::chrono_literals;

void Stats::onLoop(Clock::time_point now, bool paused)
{
    // 主循环每圈轮询一次暂停态，暂停期间按圈间隔累计
    if (m_lastLoop != Clock::time_point{} && paused) {
        m_pausedTotalSec += std::chrono::duration<double>(now - m_lastLoop).count();
    }
    m_lastLoop = now;
}

void Stats::onVideoFrame(int64_t ptsUs, int serial, Clock::time_point now)
{
    ++m_displayed;
    m_displayTimes.push_back(now);
    while (m_displayTimes.size() > 1 && now - m_displayTimes.front() > 1s) {
        m_displayTimes.pop_front();
    }

    // 结算最近一次未完成的seek：新serial首帧到达即视为seek可见
    if (!m_seeks.empty()) {
        SeekRecord& r = m_seeks.back();
        if (!r.completed && serial > r.serialAtRequest) {
            r.completed = true;
            r.latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(now - r.requestedAt).count();
        }
    }

    // pts呈现误差按serial分段：seek成功与时钟重锚都会把误差清零
    if (!m_anchored || serial != m_anchorSerial) {
        m_anchored = true;
        m_anchorSerial = serial;
        m_anchorWall = now;
        m_anchorPts = ptsUs;
        m_errN = 0;
        m_errSumUs = 0.0;
        m_errSumSqUs = 0.0;
        m_errMaxAbsUs = 0.0;
    } else {
        const double errUs = static_cast<double>(
                                 std::chrono::duration_cast<std::chrono::microseconds>(now - m_anchorWall).count())
            - static_cast<double>(ptsUs - m_anchorPts);
        ++m_errN;
        m_errSumUs += errUs;
        m_errSumSqUs += errUs * errUs;
        m_errMaxAbsUs = std::max(m_errMaxAbsUs, std::abs(errUs));
    }

    // 丢帧估算：显示pts间隔跳变超过1.8倍基准间隔，视为中间帧被丢；
    // serial变化说明是seek不连续，不算丢帧也不进入基准节拍
    if (m_lastPts != kNoPts && ptsUs > m_lastPts && serial == m_lastSerial) {
        const int64_t gap = ptsUs - m_lastPts;
        if (m_deltas.size() >= 10) {
            const int64_t med = medianFrameDeltaUs();
            if (med > 0 && gap > med * 9 / 5) {
                const uint64_t missed = static_cast<uint64_t>(std::llround(static_cast<double>(gap) / med) - 1);
                m_droppedEstimate += std::min<uint64_t>(missed, 30);
            }
        }
        if (gap < 1'000'000) { // seek产生的大间隔不进入基准节拍
            m_deltas.push_back(gap);
            if (m_deltas.size() > 121) {
                m_deltas.pop_front();
            }
        }
    }
    m_lastPts = ptsUs;
    m_lastSerial = serial;
    m_lastShownPts = ptsUs;
}

void Stats::onSeekRequested(double targetSec, int serialAtRequest, Clock::time_point now)
{
    m_seeks.push_back(SeekRecord{ targetSec, serialAtRequest, now, false, -1 });
    if (m_seeks.size() > 64) {
        m_seeks.erase(m_seeks.begin());
    }
}

void Stats::onAudioStarted(int sampleRate, int channels)
{
    m_audioSampleRate = sampleRate;
    m_audioChannels = channels;
}

void Stats::onAudioFrame(int64_t ptsUs, int samples)
{
    ++m_audioFrames;
    m_audioSamples += static_cast<uint64_t>(samples);
}

int64_t Stats::medianFrameDeltaUs() const
{
    std::vector<int64_t> v(m_deltas.begin(), m_deltas.end());
    const auto mid = v.begin() + static_cast<ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

std::string Stats::liveLine(double durationSec, bool paused, bool eof) const
{
    const double fps = static_cast<double>(m_displayTimes.size());
    std::string err = "-";
    if (m_errN > 0) {
        const double mean = m_errSumUs / m_errN;
        const double dev = std::sqrt(std::max(0.0, m_errSumSqUs / m_errN - mean * mean));
        err = std::format("{:+.1f}\u00b1{:.1f}ms", mean / 1000.0, dev / 1000.0);
    }
    const double pos = durationSec > 0 ? std::clamp(m_lastShownPts / 1e6, 0.0, durationSec) : m_lastShownPts / 1e6;
    std::string suffix;
    if (paused) {
        suffix += " [PAUSED]";
    }
    if (eof) {
        suffix += " [EOF]";
    }
    return std::format("fps {:4.1f} | disp {} drop {} stale {} null {} | err {} | pos {:.1f}s/{:.1f}s{}",
        fps, m_displayed, m_droppedEstimate, m_staleDiscarded, m_nullPulls, err, pos, durationSec, suffix);
}

void Stats::printSummary(double durationSec) const
{
    const double runSec = std::chrono::duration<double>(Clock::now() - m_runStart).count();
    const double activeSec = std::max(0.0, runSec - m_pausedTotalSec);

    std::println("========== controller benchmark 报告 ==========");
    std::println("运行时长: {:.1f}s（暂停累计 {:.1f}s，有效播放 {:.1f}s / 总时长 {:.1f}s）",
        runSec, m_pausedTotalSec, activeSec, durationSec);

    std::println("视频: 显示 {} 帧，估算丢帧 {}，seek过期帧丢弃 {}，空拉取 {}，EOF标记 {} 次",
        m_displayed, m_droppedEstimate, m_staleDiscarded, m_nullPulls, m_videoEndCount);
    if (m_errN > 0) {
        const double mean = m_errSumUs / m_errN;
        const double dev = std::sqrt(std::max(0.0, m_errSumSqUs / m_errN - mean * mean));
        std::println("  pts呈现误差: 均值 {:+.2f}ms，标准差 {:.2f}ms，最大 {:.2f}ms（n={}，按serial分段统计）",
            mean / 1000.0, dev / 1000.0, m_errMaxAbsUs / 1000.0, m_errN);
    }
    if (activeSec > 0.5) {
        std::println("  平均显示帧率: {:.1f} fps（显示帧数/有效播放时长）", m_displayed / activeSec);
    }

    const uint64_t audioSamples = m_audioSamples.load();
    std::string audioTime = "-";
    if (m_audioSampleRate > 0) {
        audioTime = std::format("{:.1f}s", static_cast<double>(audioSamples) / m_audioSampleRate);
    }
    std::println("音频: 帧 {}，采样 {}（{}），underrun {}，长饥饿 {} 次，EOF标记 {} 次，seek过期帧丢弃 {}",
        m_audioFrames.load(), audioSamples, audioTime,
        m_audioUnderruns.load(), m_audioStalls.load(),
        m_audioEndCount.load(), m_staleAudioDiscarded.load());

    if (m_seeks.empty()) {
        std::println("seek: 无");
    } else {
        int64_t total = 0;
        int64_t maxLat = 0;
        size_t completed = 0;
        for (const SeekRecord& r : m_seeks) {
            if (r.completed) {
                ++completed;
                total += r.latencyUs;
                maxLat = std::max(maxLat, r.latencyUs);
            }
        }
        std::println("seek: 共 {} 次（完成 {} 次）", m_seeks.size(), completed);
        if (completed > 0) {
            std::println("  可见延迟: 平均 {:.0f}ms，最大 {:.0f}ms（seek()调用 -> 新serial首帧显示）",
                static_cast<double>(total) / completed / 1000.0, static_cast<double>(maxLat) / 1000.0);
        }
        for (size_t i = 0; i < m_seeks.size(); ++i) {
            const SeekRecord& r = m_seeks[i];
            if (r.completed) {
                std::println("  #{:02d} -> {:7.1f}s : {:6.0f}ms", i + 1, r.targetSec, static_cast<double>(r.latencyUs) / 1000.0);
            } else {
                std::println("  #{:02d} -> {:7.1f}s : 未完成（无新serial帧显示）", i + 1, r.targetSec);
            }
        }
    }
}

} // namespace bench
