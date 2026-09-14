#include "DecodeWorker.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
// 时钟策略为false时，允许放行的阈值
constexpr int64_t CONTROL_CLOCK_THRESHOLD_US = 200000; // 200ms

std::unique_ptr<DecodeWorker> DecodeWorker::create(const Params& params)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker());
    if (worker->initialize(params)) {
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

bool DecodeWorker::initialize(const Params& params)
{
    if (!params.logger) {
        return false;
    }
    m_logger = params.logger;
    m_stream = params.stream;
    m_frameProcessors = params.frameProcessors;

    auto tracer = m_logger->trace();

    // 1.创建解码器
    m_decoder = Decoder::create(m_stream, m_logger);
    if (!m_decoder) {
        LOGE("Failed to create decoder");
        return false;
    }

    // 2. 启动解码线程
    m_exitFlag = false;
    m_thread = std::thread(&DecodeWorker::workerFunc, this);

    return true;
}

void DecodeWorker::sendPacket(PacketPtr&& packet)
{
    if (!packet) { // 正常流程解封装线程不会传入nullptr
        LOGE("Received a null packet, this should not happen.");
        return;
    }
    auto serial = packet->serial();
    if (serial != m_serial.load()) {
        // 序列号不一样，说明发生了seek，清空包队列
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
        }
        m_serial.store(serial);
    }

    std::unique_lock<std::mutex> lock(m_packetQueueMutex);
    if (m_packetQueue.size() >= m_maxPacketQueueDepth) {
        // 如果允许丢帧，压力传导到帧队列，丢且最早的帧
        if (m_canDropFrames) {
            std::lock_guard<std::mutex> frameLock(m_frameQueueMutex);
            if (!m_frameQueue.empty()) {
                m_frameQueue.pop_front();
                m_frameQueueCV.notify_one();
            }
        }
        // 等待水位下降
        m_packetQueueCV.wait(lock, [this]() {
            return m_packetQueue.size() < m_maxPacketQueueDepth || m_exitFlag;
        });
        if (m_exitFlag) {
            return;
        }
    }
    m_packetQueue.push_back(std::move(packet));
    m_packetQueueCV.notify_one();
}

FramePtr DecodeWorker::receiveFrame(int64_t playTimeUs)
{
    // 取帧不阻塞，如果队列为空则返回nullptr
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    if (m_frameQueue.empty()) {
        return nullptr;
    }
    // 先观察队头帧的pts是否可以放行
    int64_t pts = m_frameQueue.front()->pts();
    // 根据时钟策略判断是否放行
    // 默认postProcessFrame会将pts单位转换为微秒(us)
    if (m_controlClock) { // 视频模式
        if (pts > playTimeUs) {
            // 帧还没到播放时间，返回nullptr
            return nullptr;
        }
    } else { // 音频模式
        // 只有跳变超过阈值才会被时钟控制
        if (pts > playTimeUs + CONTROL_CLOCK_THRESHOLD_US) {
            // 音频帧跳变超过阈值，返回nullptr
            return nullptr;
        }
    }

    // 从帧队列中取出帧
    std::unique_ptr<Frame> frame = std::move(m_frameQueue.front());
    m_frameQueue.pop_front();
    m_frameQueueCV.notify_one();

    return frame;
}

void DecodeWorker::stop()
{
    if (m_exitFlag.exchange(true)) {
        return;
    }
    m_packetQueueCV.notify_all();
    m_frameQueueCV.notify_all();
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
    // 解码线程序列号，当与m_serial不一致时，说明发生了seek，需要刷新解码器并清空帧队列
    int serial = m_serial.load();
    while (!m_exitFlag) {
        PacketPtr packet;
        {
            std::unique_lock<std::mutex> lock(m_packetQueueMutex);
            m_packetQueueCV.wait(lock, [this]() {
                return !m_packetQueue.empty() || m_exitFlag;
            });
            if (m_exitFlag) {
                break;
            }
            packet = std::move(m_packetQueue.front());
            m_packetQueue.pop_front();
            m_packetQueueCV.notify_one();
        }
        if (!packet) { // 正常流程不会走到这里，解封装线程会保证传入的不是nullptr
            LOGE("Received a null packet, this should not happen.");
            continue;
        }

        // 检查序列号是否一致
        if (packet->serial() != serial) {
            // 序列号不一致，说明发生了seek，需要刷新解码器并清空帧队列
            {
                std::lock_guard<std::mutex> frameLock(m_frameQueueMutex);
                m_frameQueue.clear();
                m_frameQueueCV.notify_all();
            }
            if (m_decoder) {
                m_decoder->flush();
            }
            serial = packet->serial();
        }
        bool isEnd = packet->type() == Packet::PacketType::End;
        auto frames = decodePacket(std::move(packet));
        for (auto& f : frames) {
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_frameQueueCV.wait(lock, [this]() {
                return m_frameQueue.size() < m_maxFrameQueueDepth || m_exitFlag;
            });
            m_frameQueue.push_back(std::move(f));
            m_frameQueueCV.notify_all();
        }
        // 补发End帧
        if (isEnd) {
            auto endFrame = Frame::create(serial, IFrame::FrameType::End);
            if (!endFrame) {
                LOGE("Failed to create end frame");
                continue;
            }
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_frameQueue.push_back(std::move(endFrame));
            m_frameQueueCV.notify_all();
            // 不退出循环，等待seek事件
        }
    }
}

std::vector<std::unique_ptr<Frame>> DecodeWorker::decodePacket(PacketPtr&& packet)
{
    if (!m_decoder || !packet) { // 正常流程下packet不会为nullptr
        return {};
    }

    bool ret = m_decoder->sendPacket(std::move(packet));
    if (!ret) {
        LOGE("Failed to send packet to decoder");
        return {};
    }

    std::vector<std::unique_ptr<Frame>> frames;
    for (;;) {
        auto recvRet = m_decoder->receiveFrame(packet->serial());
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
        ProcessorContext context{m_stream, m_logger};
        for (auto& processor : m_frameProcessors) {
            if (!processor) {
                continue;
            }
            frame = processor->process(std::move(frame), context);
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
    }
    return std::move(frame);
}

} // namespace controller