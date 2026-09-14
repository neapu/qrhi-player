#pragma once
#include <memory>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "Decoder.h"
#include "Logger.h"
#include "FrameProcessor.h"

namespace controller {
class DecodeWorker {
public:
    struct Params {
        const AVStream* stream{nullptr};
        std::shared_ptr<Logger> logger{nullptr};
        std::vector<std::shared_ptr<FrameProcessor>> frameProcessors;
        size_t maxPacketQueueDepth{100};
        // 允许丢帧(视频)：出队侧按主时钟丢弃被新帧覆盖的到期帧
        bool canDropFrames{false};
        // 时钟策略：true=严格按时钟放行(视频)；false=时钟跳变超过阈值才限制(音频)
        bool controlClock{true};
    };
    static std::unique_ptr<DecodeWorker> create(const Params& params);

    virtual ~DecodeWorker();

    void sendPacket(PacketPtr&& packet);
    FramePtr receiveFrame(int64_t playTimeUs);

    void stop();

    int streamIndex() const;

    // 消费端累计丢弃的帧数，用于统计/调试
    uint64_t droppedFrames() const { return m_droppedFrames.load(std::memory_order_relaxed); }

protected:
    DecodeWorker();
    
    virtual bool initialize(const Params& params);

    void workerFunc();

    std::vector<std::unique_ptr<Frame>> decodePacket(PacketPtr&& packet);

    virtual std::unique_ptr<Frame> postProcessFrame(std::unique_ptr<Frame>&& frame);

protected:
    std::shared_ptr<Logger> m_logger{nullptr};
    const AVStream* m_stream{nullptr};
    std::vector<std::shared_ptr<FrameProcessor>> m_frameProcessors;

    std::deque<PacketPtr> m_packetQueue;
    std::mutex m_packetQueueMutex;
    std::condition_variable m_packetQueueCV;
    size_t m_maxPacketQueueDepth{100};

    // 这里不用FramePtr，因为FramePtr声明的是IFrame抽象类，内部处理时需要具体的Frame实现
    std::deque<std::unique_ptr<Frame>> m_frameQueue;
    std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueCV;
    size_t m_maxFrameQueueDepth{5};

    std::unique_ptr<Decoder> m_decoder{nullptr};
    std::thread m_thread;
    std::atomic_bool m_exitFlag{false};

    std::atomic_int m_serial{0};

    // 丢帧策略(仅视频)：出队侧按主时钟丢弃被覆盖的到期帧，丢弃量由实际迟到程度决定
    bool m_canDropFrames{false};
    // 时钟策略：true=严格按时钟放行(视频)，false=时钟跳变超过阈值才限制(音频)
    bool m_controlClock{true};
    // 消费端累计丢帧数，用于统计/调试
    std::atomic_uint64_t m_droppedFrames{0};
};

} // namespace controller