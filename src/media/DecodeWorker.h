#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <functional>
#include <map>
#include <queue>
#include <variant>
#include "Decoder.h"
#include "FrameProcessor.h"
#include "MediaPacket.h"
#include "media/MediaFrame.h"

namespace media {
class DecodeWorker {
public:
    struct Params {
        LoggerPtr logger{nullptr};
        FrameProcessorList frameProcessors;
        std::function<MediaPacket()> nextPacketCallback;
        int initialSerial{0};
    };
    static std::unique_ptr<DecodeWorker> create(const Params& params, DecoderPtr&& decoder);

    // 调用约定，整个生命周期只能调用一次start和stop，停止后不能再次启动
    void start();
    void stop();

    void seekRequired();

    uint32_t streamIndex() const;

    // 非阻塞，队列为空时返回nullptr
    MediaFrame nextFrame();

    bool queueEmpty() const;

    // 解码统计：只累计成功取出帧的耗时，不含输出队列满时的等待
    int64_t decodedFrames() const { return m_decodedFrames.load(std::memory_order_relaxed); }
    int64_t totalDecodeTimeUs() const { return m_totalDecodeTimeUs.load(std::memory_order_relaxed); }
    // 后处理统计：frameProcessors（Sws/Swr等）的总耗时
    int64_t totalProcessTimeUs() const { return m_totalProcessTimeUs.load(std::memory_order_relaxed); }
    // 输出队列满导致的等待总耗时（被消费端反压的证据）
    int64_t totalQueueWaitTimeUs() const { return m_totalQueueWaitTimeUs.load(std::memory_order_relaxed); }
    // 解码延迟统计：按pts配对"包发送时刻 -> 对应帧取出时刻"的穿透气延迟
    int64_t latencyFrames() const { return m_latencyFrames.load(std::memory_order_relaxed); }
    int64_t totalDecodeLatencyUs() const { return m_totalDecodeLatencyUs.load(std::memory_order_relaxed); }
private:
    explicit DecodeWorker(const Params& params, DecoderPtr&& decoder);
    bool initialize();

    void workerThread();

    fh::FramePtr processFrame(fh::FramePtr&& frame);
    void decodePacket(fh::PacketPtr&& packet, bool eof);

private:
    LoggerPtr m_logger{nullptr};
    FrameProcessorList m_frameProcessors{};
    std::function<MediaPacket()> m_nextPacketCallback{nullptr};
    DecoderPtr m_decoder{nullptr};

    std::thread m_workerThread;
    std::atomic_bool m_exitFlag{false};

    std::deque<MediaFrame> m_frameQueue;
    mutable std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueNotFullCV;

    std::atomic_bool m_seekRequired{false};
    int m_serial{0};

    std::atomic_int64_t m_decodedFrames{0};
    std::atomic_int64_t m_totalDecodeTimeUs{0};
    std::atomic_int64_t m_totalProcessTimeUs{0};
    std::atomic_int64_t m_totalQueueWaitTimeUs{0};
    std::atomic_int64_t m_latencyFrames{0};
    std::atomic_int64_t m_totalDecodeLatencyUs{0};

    // 仅解码线程访问（send/receive同线程），无需加锁；key为包pts，value为发送时刻
    std::map<int64_t, std::chrono::steady_clock::time_point> m_pendingPacketPts;

    void recordDecodeLatency(int64_t framePts, std::chrono::steady_clock::time_point recvTime);
};
using DecodeWorkerPtr = std::unique_ptr<DecodeWorker>;
} // namespace media