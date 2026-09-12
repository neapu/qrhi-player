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
        m_logger->error() << "Failed to create Demuxer";
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
            m_demuxer->seek(seekRequest.streamIndex, seekRequest.pts);
            m_serial.fetch_add(1);
        }

        auto packet = m_demuxer->readPacket(m_serial.load());
        if (!packet) {
            m_logger->warning() << "Failed to read packet";
            continue;
        }
        if (packet->type() == Packet::PacketType::End) {
            m_logger->info() << "End of stream reached";
            m_exitFlag = true;
        }
        m_onPacketRead(std::move(packet));
    }

}
} // namespace controller