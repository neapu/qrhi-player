#include "DemuxWorker.h"

namespace {
constexpr auto MAX_RETRY_COUNT = 10;
constexpr auto READ_ERROR_RETRY_DELAY = std::chrono::milliseconds(100);
void writePacketSerial(fh::PacketPtr& packet, int serial)
{
    if (packet) {
        packet->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(serial));
    }
}
}

namespace media {
std::unique_ptr<DemuxWorker> DemuxWorker::create(const Params& params, DemuxerPtr&& demuxer)
{
    auto worker = std::unique_ptr<DemuxWorker>(new DemuxWorker(params, std::move(demuxer)));
    if (worker && worker->initialize()) {
        return worker;
    }
    return nullptr;
}

DemuxWorker::DemuxWorker(const Params& params, DemuxerPtr&& demuxer)
    : m_demuxer(std::move(demuxer))
    , m_logger(params.logger)
    , m_onSeekCompleted(params.onSeekCompleted)
    , m_onError(params.onError)
{
}

DemuxWorker::~DemuxWorker()
{
    try {
        stop();
    } catch (...) {
        // Ignore exceptions during destruction
    }
}

bool DemuxWorker::initialize()
{
    FUNC_TRACE(m_logger, spdlog::level::info);
    if (!m_demuxer) {
        LOG_ERROR(m_logger, "Failed to initialize DemuxWorker: demuxer is null");
        return false;
    }
    if (!m_onSeekCompleted) {
        LOG_ERROR(m_logger, "Failed to initialize DemuxWorker: onSeekCompleted callback is null");
        return false;
    }
    if (!m_onError) {
        LOG_ERROR(m_logger, "Failed to initialize DemuxWorker: onError callback is null");
        return false;
    }
    return true;
}

void DemuxWorker::createPacketQueue(int streamIndex, size_t capacity)
{
    uint32_t streamCount = m_demuxer->streamCount();
    if (streamIndex < 0 || streamIndex >= static_cast<int>(streamCount)) {
        LOG_ERROR(m_logger, "Failed to create packet queue: invalid stream index {}", streamIndex);
        return;
    }
    m_packetQueues[streamIndex] = std::make_unique<PacketQueue>(capacity);
}

void DemuxWorker::start()
{
    m_exitFlag = false;
    m_workerThread = std::thread(&DemuxWorker::workerThread, this);
}

void DemuxWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_exitFlag = true;
        m_seekCV.notify_all();
    }
    
    stopAllPacketQueues();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void DemuxWorker::seek(int64_t positionUs)
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_seekPositionUs = positionUs;
    }
    m_seekCV.notify_all();
    interruptAllPacketQueues();
}

MediaPacket DemuxWorker::popPacket(int streamIndex)
{
    if (m_packetQueues.contains(streamIndex)) {
        return m_packetQueues[streamIndex]->pop();
    }
    return EmptyPacket{};
}

void DemuxWorker::workerThread()
{
    int retryCount = 0;
    while (!m_exitFlag) {
        int64_t seekPositionUs{AV_NOPTS_VALUE};
        {
            std::unique_lock<std::mutex> lock(m_seekMutex);
            if (m_seekPositionUs != AV_NOPTS_VALUE) {
                seekPositionUs = m_seekPositionUs;
                m_seekPositionUs = AV_NOPTS_VALUE;
            }
        }
        if (seekPositionUs != AV_NOPTS_VALUE) {
            if (m_demuxer->seek(seekPositionUs)) {
                m_serial++;
                clearAllPacketQueues();
                cancelInterruptAllPacketQueues();
                pushFlashPacketToAllQueues();
                m_onSeekCompleted(true, m_serial);
            } else {
                cancelInterruptAllPacketQueues();
                LOG_ERROR(m_logger, "Failed to seek to position {}", seekPositionUs);
                m_onSeekCompleted(false, m_serial);
            }
        }
        auto packetExp = m_demuxer->readPacket();
        if (!packetExp) {
            int err = packetExp.error();
            if (err == AVERROR_EOF) {
                // 文件末尾，插入结束标记到包队列中
                pushEndPacketToAllQueues();
                // 等待 seek 操作
                std::unique_lock<std::mutex> lock(m_seekMutex);
                m_seekCV.wait(lock, [this] { return m_seekPositionUs != AV_NOPTS_VALUE || m_exitFlag; });
                continue;
            }
            if (retryCount < MAX_RETRY_COUNT) {
                retryCount++;
                std::this_thread::sleep_for(READ_ERROR_RETRY_DELAY);
            } else {
                // wait for seek or exit signal
                std::unique_lock<std::mutex> lock(m_seekMutex);
                m_seekCV.wait(lock, [this] { return m_seekPositionUs != AV_NOPTS_VALUE || m_exitFlag; });
                retryCount = 0;
            }

            continue;
        }
        retryCount = 0; // Reset retry count after a successful read
        // 将读取到的包分发到对应的包队列中
        auto packet = std::move(*packetExp);
        writePacketSerial(packet, m_serial);
        if (m_packetQueues.contains(packet->stream_index)) {
            m_packetQueues[packet->stream_index]->push(std::move(packet), false);
        }
    }
}

void DemuxWorker::stopAllPacketQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->stop();
    }
}

void DemuxWorker::interruptAllPacketQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->interrupt();
    }
}

void DemuxWorker::clearAllPacketQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->clear();
    }
}

void DemuxWorker::pushFlashPacketToAllQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->push(FlashPacket{m_serial}, true);
    }
}

void DemuxWorker::pushEndPacketToAllQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->push(EndPacket{m_serial}, true);
    }
}
void DemuxWorker::cancelInterruptAllPacketQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue->cancelInterrupt();
    }
}

} // namespace media