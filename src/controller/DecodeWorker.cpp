#include "DecodeWorker.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
std::unique_ptr<DecodeWorker> DecodeWorker::create(const Params& params, DecoderPtr&& decoder)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker());
    if (worker->initialize(params, std::move(decoder))) {
        return worker;
    }
    return nullptr;
}

DecodeWorker::DecodeWorker()
{
}

DecodeWorker::~DecodeWorker()
{
    stop();
}

bool DecodeWorker::initialize(const Params& params, DecoderPtr&& decoder)
{
    if (!params.logger) {
        return false;
    }
    m_decoder = std::move(decoder);
    m_logger = params.logger;
    m_stream = params.stream;
    m_frameProcessors = params.frameProcessors;
    m_packetQueue = params.packetQueue;
    m_frameQueue = params.frameQueue;

    FUNC_TRACE();

    if (!m_decoder) {
        LOGE("Decoder is not provided.");
        return false;
    }
    if (!m_packetQueue || !m_frameQueue) {
        LOGE("Queues are not provided.");
        return false;
    }

    m_thread = std::thread(&DecodeWorker::workerFunc, this);

    return true;
}

void DecodeWorker::stop()
{
    // 先关闭两个队列：既解除本线程在pop/push上的等待，
    // 也解除解封装线程在包队列满上的阻塞。close()幂等，可重复调用
    if (m_packetQueue) {
        m_packetQueue->close();
    }
    if (m_frameQueue) {
        m_frameQueue->close();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

int DecodeWorker::streamIndex() const
{
    return m_stream ? m_stream->index : -1;
}

void DecodeWorker::workerFunc()
{
    FUNC_TRACE();
    // 解码线程序列号：取出的包与其不一致时说明发生了seek换代，需要刷新解码器。
    // 包与帧的代次作废由Controller在seek成功点完成(见Controller::onSeekSucceeded)，
    // 这里只负责解码器自身的状态
    int serial = 0;
    while (true) {
        PacketPtr packet;
        if (!m_packetQueue->pop(packet)) {
            break; // 队列已关闭，停止解码线程
        }

        // 检查序列号是否一致
        if (packet->serial() != serial) {
            // 序列号不一致，说明发生了seek，需要刷新解码器
            if (m_decoder) {
                m_decoder->flush();
            }
            serial = packet->serial();
        }
        bool isEnd = packet->type() == Packet::PacketType::End;
        auto frames = decodePacket(std::move(packet));
        for (auto& f : frames) {
            auto result = m_frameQueue->push(std::move(f));
            if (result == FrameQueue::PushResult::Closed) {
                return; // 队列已关闭，停止解码线程
            }
            if (result == FrameQueue::PushResult::StaleRejected) {
                // 帧属已作废的旧代次：丢弃本包剩余帧，回到取包路径，
                // 由下一个(新代次的)包触发解码器flush
                LOGD("Discarded frames of stale serial " << serial << " after seek");
                break;
            }
            m_decodedFrames++;
        }
        // 补发End帧
        if (isEnd) {
            auto endFrame = Frame::create(serial, IFrame::FrameType::End);
            if (!endFrame) {
                LOGE("Failed to create end frame");
                continue;
            }
            // 标记帧不受水位限制，避免消费侧停住时"结尾"信号被卡在队满等待里；
            // StaleRejected表示该End帧属旧代次，seek后不应再交付给消费侧，丢弃即可
            auto result = m_frameQueue->push(std::move(endFrame), false);
            if (result == FrameQueue::PushResult::Closed) {
                return; // 队列已关闭，停止解码线程
            }
            // 不退出循环，等待seek事件
        }
    }
}

std::vector<std::unique_ptr<Frame>> DecodeWorker::decodePacket(PacketPtr&& packet)
{
    if (!m_decoder || !packet) { // 正常流程下packet不会为nullptr
        return {};
    }

    int serial = packet->serial();
    bool ret = m_decoder->sendPacket(std::move(packet));
    if (!ret) {
        LOGE("Failed to send packet to decoder");
        return {};
    }

    std::vector<std::unique_ptr<Frame>> frames;
    for (;;) {
        auto recvRet = m_decoder->receiveFrame(serial);
        if (!recvRet) {
            int err = recvRet.error();
            if (err != AVERROR(EAGAIN) && err != AVERROR_EOF) {
                LOGE("Failed to receive frame from decoder, error: " << fh::err2str(err));
            }
            break;
        }
        auto processedFrame = postProcessFrame(std::move(recvRet.value()));
        if (processedFrame) {
            frames.push_back(std::move(processedFrame));
        }
    }
    return frames;
}

std::unique_ptr<Frame> DecodeWorker::postProcessFrame(std::unique_ptr<Frame>&& frame)
{
    if (!frame) { // 正常情况下不会为nullptr
        return nullptr;
    }

    // 按顺序执行处理链，处理器返回nullptr表示处理失败，丢弃该帧
    if (!m_frameProcessors.empty()) {
        for (auto& processor : m_frameProcessors) {
            if (!processor) {
                continue;
            }
            frame = processor->process(std::move(frame));
            if (!frame) {
                return nullptr;
            }
        }
    }

    // 处理链输出的帧不会设置timebase，需要回退用流的timebase
    auto timebase = frame->timebase();
    if (timebase.num == 0) {
        timebase = m_stream ? m_stream->time_base : AVRational{0, 1};
    }
    // pts单位转换为微秒
    if (frame->pts() != AV_NOPTS_VALUE && timebase.num != 0) {
        int64_t ptsUs = av_rescale_q(frame->pts(), timebase, AVRational{1, 1000000});
        frame->setPts(ptsUs);
        frame->setTimebase(AVRational{1, 1000000});
    }
    return std::move(frame);
}

} // namespace controller