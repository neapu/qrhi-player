#pragma once
#include <memory>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "Decoder.h"
#include "Logger.h"

namespace controller {
class DecodeWorker {
public:
    static std::unique_ptr<DecodeWorker> create(const AVStream* stream, std::shared_ptr<Logger> logger);

    virtual ~DecodeWorker();

    void sendPacket(PacketPtr&& packet);
    FramePtr receiveFrame(int64_t playTimeUs);

    void stop();

protected:
    DecodeWorker();
    
    bool initialize(const AVStream* stream, std::shared_ptr<Logger> logger);

    void workerFunc();

    // 单独传一个序列号是因为结尾时需要传一个nullptr冲刷解码器，所以不能从packet中获取序列号
    std::vector<std::unique_ptr<Frame>> decodePacket(PacketPtr&& packet, int serial);

    virtual std::unique_ptr<Frame> postProcessFrame(std::unique_ptr<Frame>&& frame);

protected:
    std::shared_ptr<Logger> m_logger{nullptr};
    const AVStream* m_stream{nullptr};

    std::deque<PacketPtr> m_packetQueue;
    std::mutex m_packetQueueMutex;
    std::condition_variable m_packetQueueCV;
    size_t m_maxPacketQueueDepth{0};

    // 这里不用FramePtr，因为FramePtr声明的是IFrame抽象类，内部处理时需要具体的Frame实现
    std::deque<std::unique_ptr<Frame>> m_frameQueue;
    std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueCV;
    size_t m_maxFrameQueueDepth{0};

    std::unique_ptr<Decoder> m_decoder{nullptr};
    std::thread m_thread;
    std::atomic_bool m_exitFlag{false};

    std::atomic_int m_serial{0};

    // 丢帧策略
    bool m_canDropFrames{false};
    // 时钟策略
    bool m_controlClock{true};
};

} // namespace controller