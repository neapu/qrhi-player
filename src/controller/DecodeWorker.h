#pragma once
#include <memory>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "Decoder.h"

namespace controller {
class DecodeWorker {
public:
    static std::unique_ptr<DecodeWorker> create(const AVStream* stream);

    virtual ~DecodeWorker() = default;

    void sendPacket(const PacketPtr& packet);
    FramePtr receiveFrame();

private:
    DecodeWorker() = default;
    bool initialize(const AVStream* stream);

    void workerFunc();

private:
    std::deque<PacketPtr> m_packetQueue;
    std::mutex m_packetQueueMutex;
    std::condition_variable m_packetQueueCV;

    std::deque<FramePtr> m_frameQueue;
    std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueCV;

    std::unique_ptr<Decoder> m_decoder{nullptr};
    std::thread m_thread;
    std::atomic<bool> m_exitFlag{false};
};

} // namespace controller