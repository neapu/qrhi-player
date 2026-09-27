#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
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
};
using DecodeWorkerPtr = std::unique_ptr<DecodeWorker>;
} // namespace media