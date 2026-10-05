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
DemuxWorkerBuilder::DemuxWorkerBuilder(const DemuxWorker::Params& params, DemuxerPtr&& demuxer)
    : m_params(params)
    , m_demuxer(std::move(demuxer))
{
}

DemuxWorkerBuilder& DemuxWorkerBuilder::withPacketQueue(int streamIndex, size_t capacity)
{
    m_packetQueueSpecs.emplace_back(streamIndex, capacity);
    return *this;
}

std::unique_ptr<DemuxWorker> DemuxWorkerBuilder::build()
{
    auto worker = std::unique_ptr<DemuxWorker>(new DemuxWorker(m_params, std::move(m_demuxer)));
    if (worker->initialize() && worker->configurePacketQueues(m_packetQueueSpecs)) {
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
    stopInternal();
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

bool DemuxWorker::configurePacketQueues(const std::vector<std::pair<int, size_t>>& packetQueueSpecs)
{
    uint32_t streamCount = m_demuxer->streamCount();
    for (const auto& [streamIndex, capacity] : packetQueueSpecs) {
        if (streamIndex < 0 || streamIndex >= static_cast<int>(streamCount)) {
            LOG_ERROR(m_logger, "Failed to create packet queue: invalid stream index {}", streamIndex);
            return false;
        }
        m_packetQueues[streamIndex] = std::make_unique<PacketQueue>(capacity);
    }
    return true;
}

int DemuxWorker::start()
{
    if (m_started.exchange(true)) {
        LOG_ERROR(m_logger, "DemuxWorker::start called more than once, the extra call is ignored");
        return m_serial;
    }
    m_exitFlag = false;
    m_workerThread = std::thread(&DemuxWorker::workerThread, this);
    return m_serial;
}

void DemuxWorker::stop()
{
    if (m_stopped.exchange(true)) {
        LOG_ERROR(m_logger, "DemuxWorker::stop called more than once, the extra call is ignored");
        return;
    }
    FUNC_TRACE(m_logger, spdlog::level::info);
    stopInternal();
}

void DemuxWorker::stopInternal()
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_exitFlag = true;
        m_seekCV.notify_all();
    }

    stopAllPacketQueues();
    if (!m_workerThread.joinable()) {
        return;
    }
    // 回调运行在解封装线程，此处 join 自身会抛 std::system_error 并终止进程
    if (m_workerThread.get_id() == std::this_thread::get_id()) {
        LOG_ERROR(m_logger, "DemuxWorker::stop must not be called from the demux thread, the worker thread is detached");
        m_workerThread.detach();
        return;
    }
    m_workerThread.join();
}

void DemuxWorker::seek(int64_t positionUs)
{
    interruptAllPacketQueues();
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_seekPositionUs = positionUs;
    }
    m_seekCV.notify_all();
}

MediaPacket DemuxWorker::popPacket(int streamIndex)
{
    auto it = m_packetQueues.find(streamIndex);
    if (it != m_packetQueues.end()) {
        return it->second->pop();
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
                retryCount = 0;
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
                m_onError(DemuxWorker::Error::ReadFailed);
                LOG_ERROR(m_logger, "Failed to read packet after {} retries, error {}", MAX_RETRY_COUNT, err);
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
        auto it = m_packetQueues.find(packet->stream_index);
        if (it != m_packetQueues.end()) {
            it->second->push(std::move(packet), false);
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