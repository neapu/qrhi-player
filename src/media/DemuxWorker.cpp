#include "DemuxWorker.h"

namespace {
constexpr auto TARGET_QUEUE_DURATION = 5; // in seconds
constexpr auto MAX_READ_ERROR_COUNT = 10;

size_t targetQueueSize(AVFormatContext* fmtCtx, AVStream* stream, media::LoggerPtr logger)
{
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        // 目标时长的包数量 = 目标时长 * 采样率 / 每个音频帧的采样数
        // 如果获取不到，采样率按高的算，样本容量按小的算，结果宁大勿小
        auto sampleRate = stream->codecpar->sample_rate;
        sampleRate = sampleRate > 0 ? sampleRate : 192000; // 如果采样率无效，使用默认值 192000 Hz
        auto frameSize = stream->codecpar->frame_size;
        frameSize = frameSize > 0 ? frameSize : 1024; // 如果帧大小无效，使用默认值 1024
        return TARGET_QUEUE_DURATION * sampleRate / frameSize;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 目标时长的包数量 = 目标时长 * 帧率
        auto frameRate = av_guess_frame_rate(fmtCtx, stream, nullptr);
        if (frameRate.num == 0) {
            frameRate = {120, 1}; // 必须有兜底，按大的来
            LOG_WARN(logger, "Failed to guess frame rate, using default 120 fps");
        }
        return TARGET_QUEUE_DURATION * frameRate.num / frameRate.den; 
    }
    return 0;
}

void setPacketSerial(fh::PacketPtr& packet, int serial)
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
    if (worker->initialize()) {
        return worker;
    }
    return nullptr;
}

DemuxWorker::DemuxWorker(const Params& params, DemuxerPtr&& demuxer)
    : m_logger(params.logger),
      m_mainStreamIndex(params.mainStreamIndex),
      m_demuxer(std::move(demuxer)),
      m_serial(params.initialSerial)
{
}

bool DemuxWorker::initialize()
{
    FUNC_TRACE(m_logger, spdlog::level::info);
    if (!m_demuxer) {
        LOG_ERROR(m_logger, "Demuxer is empty");
        return false;
    }

    createPacketQueues();

    return true;
}

void DemuxWorker::createPacketQueues()
{
    m_packetQueues.clear();
    uint32_t streamCount = m_demuxer->streamCount();
    for (uint32_t i = 0; i < streamCount; ++i) {
        auto stream = m_demuxer->stream(i);
        auto queueSize = targetQueueSize(m_demuxer->formatContext(), stream, m_logger);
        if (queueSize == 0) continue;
        bool dropOldest = m_mainStreamIndex != i;
        // PacketQueue 含 mutex/condition_variable，不可拷贝也不可移动，
        // 必须用 try_emplace 就地构造
        m_packetQueues.try_emplace(i, queueSize, dropOldest);
    }
}

void DemuxWorker::setMainStream(uint32_t streamIndex)
{
    if (m_started) {
        LOG_WARN(m_logger, "setMainStream must be called before start, ignored");
        return;
    }
    m_mainStreamIndex = streamIndex;
    // 队列策略由主流派生，start 之前队列为空且无线程访问，直接重建
    createPacketQueues();
}

void DemuxWorker::interruptMainStreamQueue()
{
    if (m_packetQueues.contains(m_mainStreamIndex)) {
        m_packetQueues.at(m_mainStreamIndex).interrupt();
    } else {
        LOG_WARN(m_logger, "Main stream index {} not found in packet queues", m_mainStreamIndex);
    }
}

void DemuxWorker::workerThread()
{
    if (!m_demuxer) {
        LOG_ERROR(m_logger, "Demuxer is empty");
        return;
    }
    int readErrorCount{0};
    int threadSerial{m_serial};
    while (!m_exitFlag) {
        int64_t seekTargetUs{AV_NOPTS_VALUE};
        {
            std::unique_lock<std::mutex> lock(m_seekMutex);
            seekTargetUs = m_seekTargetUs;
            m_seekTargetUs = AV_NOPTS_VALUE;
        }
        if (seekTargetUs != AV_NOPTS_VALUE) {
            if (m_demuxer->seek(seekTargetUs)) {
                clearAllPacketQueues();
                threadSerial = m_serial;
                m_endOfFile = false;
            }
        }
        auto packetExp = m_demuxer->readPacket();
        if (!packetExp) {
            int err = packetExp.error();
            if (err == AVERROR_EOF) {
                m_endOfFile = true;
                // 所有流都插入一个 EndPacket
                for (auto& [index, queue] : m_packetQueues) {
                    queue.push(EndPacket{});
                }
                // 等待 seek 请求
                std::unique_lock<std::mutex> lock(m_seekMutex);
                m_seekCV.wait(lock, [this] { return m_seekTargetUs != AV_NOPTS_VALUE || m_exitFlag; });
            } else {
                readErrorCount++;
                if (readErrorCount >= MAX_READ_ERROR_COUNT) {
                    LOG_ERROR(m_logger, "Too many consecutive read errors, exiting demux loop");
                    m_endOfFile = true;
                    std::unique_lock<std::mutex> lock(m_seekMutex);
                    m_seekCV.wait(lock, [this] { return m_seekTargetUs != AV_NOPTS_VALUE || m_exitFlag; });
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            readErrorCount = 0;
            auto packet = std::move(packetExp.value());
            setPacketSerial(packet, threadSerial);
            if (m_packetQueues.contains(packet->stream_index)) {
                m_packetQueues.at(packet->stream_index).push(std::move(packet));
            } else {
                LOG_WARN(m_logger, "Stream index {} not found in packet queues", packet->stream_index);
            }
        }
    }
}

void DemuxWorker::clearAllPacketQueues()
{
    for (auto& [index, queue] : m_packetQueues) {
        queue.clear();
    }
}

void DemuxWorker::start()
{
    m_exitFlag = false;
    m_started = true;
    m_workerThread = std::thread(&DemuxWorker::workerThread, this);
}

void DemuxWorker::stop()
{
    m_exitFlag = true;
    m_seekCV.notify_all();
    interruptMainStreamQueue();
    clearAllPacketQueues();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

MediaPacket DemuxWorker::nextPacket(uint32_t streamIndex)
{
    if (m_packetQueues.contains(streamIndex)) {
        return m_packetQueues.at(streamIndex).pop();
    }
    LOG_WARN(m_logger, "Stream index {} not found in packet queues", streamIndex);
    return EmptyPacket{};
}

int DemuxWorker::seek(int64_t us)
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_seekTargetUs = us;
        m_serial++;
    }
    m_seekCV.notify_all();
    interruptMainStreamQueue();
    return m_serial;
}

bool DemuxWorker::streamQueueEmpty(uint32_t streamIndex) const
{
    if (m_packetQueues.contains(streamIndex)) {
        return m_packetQueues.at(streamIndex).empty();
    }
    LOG_WARN(m_logger, "Stream index {} not found in packet queues", streamIndex);
    return true;
}

} // namespace media