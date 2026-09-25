#include "DecodeWorker.h"

namespace {
int getPacketSerial(const fh::PacketPtr& packet)
{
    if (packet && packet->opaque) {
        return static_cast<int>(reinterpret_cast<intptr_t>(packet->opaque));
    }
    return 0;
}
}

namespace media {
std::unique_ptr<DecodeWorker> DecodeWorker::create(const Params& params, DecoderPtr&& decoder)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker(params, std::move(decoder)));
    if (worker->initialize()) {
        return worker;
    }
    return nullptr;
}

DecodeWorker::DecodeWorker(const Params& params, DecoderPtr&& decoder)
    : m_logger(params.logger),
      m_frameProcessors(params.frameProcessors),
      m_nextPacketCallback(params.nextPacketCallback),
      m_decoder(std::move(decoder))
{
}

bool DecodeWorker::initialize()
{
    FUNC_TRACE(m_logger, spdlog::level::info);
    if (!m_decoder) {
        LOG_ERROR(m_logger, "Decoder is not initialized");
        return false;
    }
    if (!m_nextPacketCallback) {
        LOG_ERROR(m_logger, "Next packet callback is not set");
        return false;
    }
    return true;
}

void DecodeWorker::start()
{
    FUNC_TRACE(m_logger, spdlog::level::info);
    m_exitFlag = false;
    m_workerThread = std::thread(&DecodeWorker::workerThread, this);
}

void DecodeWorker::stop()
{
    FUNC_TRACE(m_logger, spdlog::level::info);
    m_exitFlag = true;
    m_frameQueueNotFullCV.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void DecodeWorker::seekRequired()
{
    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
    m_seekRequired = true;
}

uint32_t DecodeWorker::streamIndex() const
{
    return m_decoder->stream()->index;
}

fh::FramePtr DecodeWorker::nextFrame()
{
    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
    if (m_frameQueue.empty()) {
        return nullptr;
    }
    auto frame = std::move(m_frameQueue.front());
    m_frameQueue.pop_front();
    m_frameQueueNotFullCV.notify_all();
    return frame;
}

bool DecodeWorker::queueEmpty() const
{
    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
    return m_frameQueue.empty();
}

void DecodeWorker::workerThread()
{
    while (!m_exitFlag) {
        {
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_seekRequired = false; // 标识只是为了打断入队阻塞，真正的清理由序列号变化触发
        }
        
        auto packet = m_nextPacketCallback();
        if (!packet) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        int packetSerial = getPacketSerial(packet);
        if (packetSerial != m_serial) {
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_frameQueue.clear();
            m_decoder->flush();
            m_serial = packetSerial;
        }
        bool ret = m_decoder->sendPacket(std::move(packet));
        if (!ret) {
            continue;
        }
        for (;;) {
            auto frameResult = m_decoder->receiveFrame();
            if (!frameResult) {
                int err = frameResult.error();
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame from decoder, error: {}", err);
                break;
            }
            auto processedFrame = processFrame(std::move(frameResult.value()));
            if (processedFrame) {
                std::unique_lock<std::mutex> lock(m_frameQueueMutex);
                m_frameQueueNotFullCV.wait(lock, [this]() { 
                    return m_frameQueue.size() < 10 || m_exitFlag || m_seekRequired; 
                });
                if (m_exitFlag || m_seekRequired) {
                    break;
                }
                m_frameQueue.push_back(std::move(processedFrame));
            }
        }
    }
}

fh::FramePtr DecodeWorker::processFrame(fh::FramePtr&& frame)
{
    for (auto& processor : m_frameProcessors) {
        frame = processor->process(std::move(frame));
        if (!frame) {
            return nullptr;
        }
    }
    return std::move(frame);
}

} // namespace media