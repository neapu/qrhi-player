#include "DemuxerWorker.h"

namespace controller {
std::unique_ptr<DemuxerWorker> DemuxerWorker::create(const Params& params)
{
    auto worker = std::unique_ptr<DemuxerWorker>(new DemuxerWorker());
    if (!worker->initialize(params)) {
        return nullptr;
    }
    return worker;
}

DemuxerWorker::~DemuxerWorker()
{
    stop();
}

void DemuxerWorker::stop()
{
    if (m_exitFlag.exchange(true)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void DemuxerWorker::seek(int streamIndex, int64_t pts)
{
    m_seekRequest.store({streamIndex, pts});
}

bool DemuxerWorker::initialize(const Params& params)
{
    if (!params.logger || !params.onPacketRead) {
        return false;
    }
    m_logger = params.logger;
    auto tracer = m_logger->trace();

    m_onPacketRead = params.onPacketRead;
    m_demuxer = Demuxer::create(params.url, params.logger);
    if (!m_demuxer) {
        LOGE("Failed to create Demuxer");
        return false;
    }
    m_exitFlag = false;
    m_thread = std::thread(&DemuxerWorker::workerFunc, this);
    
    return true;
}

void DemuxerWorker::workerFunc()
{
    auto tracer = m_logger->trace();
    while (!m_exitFlag) {
        auto seekRequest = m_seekRequest.exchange({0, AV_NOPTS_VALUE});
        if (seekRequest.pts != AV_NOPTS_VALUE) {
            if (!m_demuxer->seek(seekRequest.streamIndex, seekRequest.pts)) {
                LOGE("Failed to seek");
            } else {
                m_serial.fetch_add(1);
            }
        }

        auto packet = m_demuxer->readPacket(m_serial.load());
        if (!packet) {
            LOGE("Failed to read packet");
            // 暂时只考虑本地文件，调试阶段先直接中止程序
            std::abort();
            break;
        }
        
        if (packet->type() == Packet::PacketType::End) {
            LOGI("End of stream reached");
            m_exitFlag = true;
        }
        m_onPacketRead(std::move(packet));
    }

}
} // namespace controller