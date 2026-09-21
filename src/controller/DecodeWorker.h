#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include "Decoder.h"
#include "Logger.h"
#include "FrameProcessor.h"
#include "PacketQueue.h"
#include "FrameQueue.h"

namespace controller {
/**
 * @brief 解码线程：独占Decoder与帧处理器链，解码只在自身线程上进行。
 *
 * 与外界的两端都通过被动队列交接，因此本类不暴露任何"会被别的线程调用"的方法：
 * - 入口：PacketQueue(生产者=解封装线程，消费者=本线程)
 * - 出口：FrameQueue(生产者=本线程，消费者=UI/音频线程)
 * 除stop()外，本类的方法只允许在所属线程上调用；stop()只允许由持有者(Controller)调用。
 */
class DecodeWorker {
public:
    struct Params {
        const AVStream* stream{nullptr};
        std::shared_ptr<Logger> logger{nullptr};
        FrameProcessorList frameProcessors;
        // 裸指针安全声明：只借用，不管理生命周期。队列由Controller持有，
        // 且必须比本worker长寿(Controller析构时先stop()再销毁队列)
        PacketQueue* packetQueue{nullptr};
        FrameQueue* frameQueue{nullptr};
    };
    static std::unique_ptr<DecodeWorker> create(const Params& params, DecoderPtr&& decoder);

    virtual ~DecodeWorker();

    // 停止解码线程：关闭两个队列(同时解除两端阻塞)后回收线程。调用后不可重启
    void stop();

    int streamIndex() const;

    // 已打开的解码上下文，输出帧的参数以此为准；create失败时为nullptr
    const AVCodecContext* codecContext() const { return m_decoder ? m_decoder->codecContext() : nullptr; }

    // 解码线程累计解码的帧数，用于统计/调试
    uint64_t decodedFrames() const { return m_decodedFrames.load(std::memory_order_relaxed); }

protected:
    DecodeWorker();
    
    virtual bool initialize(const Params& params, DecoderPtr&& decoder);

    void workerFunc();

    std::vector<std::unique_ptr<Frame>> decodePacket(PacketPtr&& packet);

    virtual std::unique_ptr<Frame> postProcessFrame(std::unique_ptr<Frame>&& frame);

protected:
    std::shared_ptr<Logger> m_logger{nullptr};
    const AVStream* m_stream{nullptr};
    FrameProcessorList m_frameProcessors;
    // 裸指针安全声明：只借用，不管理生命周期(见Params)
    PacketQueue* m_packetQueue{nullptr};
    FrameQueue* m_frameQueue{nullptr};

    std::unique_ptr<Decoder> m_decoder{nullptr};
    std::thread m_thread;
    // 解码线程累计解码的帧数，用于统计/调试
    std::atomic_uint64_t m_decodedFrames{0};
};

} // namespace controller