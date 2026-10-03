#include "DemuxWorker.h"

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
{
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
    m_exitFlag = true;
    stopAllPacketQueues();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void DemuxWorker::seek(int64_t positionUs)
{
    m_seekPositionUs = positionUs;
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
    while (!m_exitFlag) {
        auto seekPositionUs = m_seekPositionUs.exchange(AV_NOPTS_VALUE);
        if (seekPositionUs != AV_NOPTS_VALUE) {
            if (m_demuxer->seek(seekPositionUs)) {
                m_serial++;
                clearAllPacketQueues();
                pushFlashPacketToAllQueues();
                m_onSeekCompleted(true, m_serial);
            } else {
                LOG_ERROR(m_logger, "Failed to seek to position {}", seekPositionUs);
                m_onSeekCompleted(false, m_serial);
            }
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
        queue->push(FlashPacket{m_serial});
    }
}

} // namespace media