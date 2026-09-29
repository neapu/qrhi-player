#include "DecodeWorker.h"

#include <chrono>

namespace {
int getPacketSerial(const fh::PacketPtr& packet)
{
    if (packet && packet->opaque) {
        return static_cast<int>(reinterpret_cast<intptr_t>(packet->opaque));
    }
    return 0;
}

void setFrameSerial(const fh::FramePtr& frame, int serial)
{
    if (frame) {
        frame->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(serial));
    }
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
      m_decoder(std::move(decoder)),
      m_serial(params.initialSerial)
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
    m_frameQueueNotFullCV.notify_all();
}

uint32_t DecodeWorker::streamIndex() const
{
    return m_decoder->stream()->index;
}

MediaFrame DecodeWorker::nextFrame()
{
    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
    if (m_frameQueue.empty()) {
        return EmptyFrame{};
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
    bool eof{false};
    while (!m_exitFlag) {
        m_seekRequired = false; // 标识只是为了打断入队阻塞，真正的清理由序列号变化触发
        
        auto packetVariant = m_nextPacketCallback();
        std::visit([this, &eof](auto&& arg) {
            if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, EndPacket>) {
                EndPacket endPacket = std::move(arg);
                if (endPacket.serial > m_serial) {
                    m_serial = endPacket.serial;
                }
                if (!eof) {
                    eof = true;
                    decodePacket(nullptr, eof); // Indicate end of stream to the decoder
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, fh::PacketPtr>) {
                int packetSerial = getPacketSerial(arg);
                if (packetSerial > m_serial) {
                    eof = false; // Reset EOF flag when a new serial is encountered
                    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
                    m_frameQueue.clear();
                    m_decoder->flush();
                    m_pendingPacketPts.clear();
                    m_serial = packetSerial;
                }
                decodePacket(std::move(arg), eof);
            }
        }, std::move(packetVariant));
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

void DecodeWorker::recordDecodeLatency(int64_t framePts, std::chrono::steady_clock::time_point recvTime)
{
    if (framePts == AV_NOPTS_VALUE || m_pendingPacketPts.empty()) {
        return;
    }
    // 输出帧的pts之前的包不会再有输出（画面被跳过），直接清理，防止map无限增长
    m_pendingPacketPts.erase(m_pendingPacketPts.begin(), m_pendingPacketPts.lower_bound(framePts));
    auto it = m_pendingPacketPts.find(framePts);
    if (it != m_pendingPacketPts.end()) {
        m_totalDecodeLatencyUs.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(recvTime - it->second).count(),
            std::memory_order_relaxed);
        m_latencyFrames.fetch_add(1, std::memory_order_relaxed);
        m_pendingPacketPts.erase(it);
    }
}

void DecodeWorker::decodePacket(fh::PacketPtr&& packet, bool eof)
{
    const auto sendTime = std::chrono::steady_clock::now();
    if (packet && packet->pts != AV_NOPTS_VALUE) {
        if (m_pendingPacketPts.size() >= 4096) {
            m_pendingPacketPts.erase(m_pendingPacketPts.begin());
        }
        m_pendingPacketPts[packet->pts] = sendTime;
    }
    bool ret = m_decoder->sendPacket(std::move(packet));
    if (!ret) {
        return;
    }
    for (;;) {
        const auto decodeStart = std::chrono::steady_clock::now();
        auto frameResult = m_decoder->receiveFrame();
        if (!frameResult) {
            int err = frameResult.error();
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            }
            LOG_ERROR(m_logger, "Failed to receive frame from decoder, error: {}", err);
            break;
        }
        const auto recvTime = std::chrono::steady_clock::now();
        m_totalDecodeTimeUs.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(recvTime - decodeStart).count(),
            std::memory_order_relaxed);
        m_decodedFrames.fetch_add(1, std::memory_order_relaxed);
        recordDecodeLatency(frameResult.value()->pts, recvTime);
        const auto processStart = std::chrono::steady_clock::now();
        auto processedFrame = processFrame(std::move(frameResult.value()));
        m_totalProcessTimeUs.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - processStart).count(),
            std::memory_order_relaxed);
        if (processedFrame) {
            setFrameSerial(processedFrame, m_serial);
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            const auto waitStart = std::chrono::steady_clock::now();
            m_frameQueueNotFullCV.wait(lock, [this]() {
                return m_frameQueue.size() < 10 || m_exitFlag || m_seekRequired;
            });
            m_totalQueueWaitTimeUs.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - waitStart).count(),
                std::memory_order_relaxed);
            if (m_exitFlag || m_seekRequired) {
                break;
            }
            m_frameQueue.push_back(std::move(processedFrame));
        }
    }
    if (eof && !m_seekRequired && !m_exitFlag) {
        std::unique_lock<std::mutex> lock(m_frameQueueMutex);
        m_frameQueue.push_back(EndFrame{m_serial});
    }
}

} // namespace media