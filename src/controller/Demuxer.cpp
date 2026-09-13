#include "Demuxer.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
std::unique_ptr<Demuxer> Demuxer::create(const std::string& url, std::shared_ptr<Logger> logger)
{
    auto demuxer = std::unique_ptr<Demuxer>(new Demuxer());
    if (demuxer->initialize(url, logger)) {
        return demuxer;
    }
    return nullptr;
}

bool Demuxer::initialize(const std::string& url, std::shared_ptr<Logger> logger)
{
    if (!logger) {
        return false;
    }
    m_logger = logger;
    auto tracer = m_logger->trace();
    
    auto ret = fh::makeInputContext(url.c_str());
    if (!ret) {
        int err = ret.error();
        LOGE("Failed to create input context, error code: " << fh::err2str(err));
        return false;
    }
    m_inputContext = std::move(ret.value());
    return true;
}

int Demuxer::streamCount() const
{
    return m_inputContext ? m_inputContext->nb_streams : 0;
}

const AVStream* Demuxer::stream(int index) const
{
    if (!m_inputContext || index < 0 || index >= m_inputContext->nb_streams) {
        LOGW("Invalid stream index: " << index);
        return nullptr;
    }
    return m_inputContext->streams[index];
}

controller::PacketPtr Demuxer::readPacket(int serial)
{
    if (!m_inputContext) {
        LOGE("Input context is not initialized");
        return nullptr;
    }
    PacketPtr packet = Packet::create(serial, Packet::PacketType::Normal);
    if (!packet) {
        LOGE("Failed to create packet");
        return nullptr;
    }
    int ret = av_read_frame(m_inputContext.get(), packet->avPacket());
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            packet->setType(Packet::PacketType::End);
            LOGI("End of file reached");
        } else {
            LOGE("Failed to read packet, error code: " << fh::err2str(ret));
            return nullptr;
        }
    }

    return packet;
}

bool Demuxer::seek(int streamIndex, int64_t pts)
{
    if (!m_inputContext) {
        LOGE("Input context is not initialized");
        return false;
    }
    int ret = av_seek_frame(m_inputContext.get(), streamIndex, pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE("Failed to seek, error code: " << fh::err2str(ret));
        return false;
    }
    return true;
}

} // namespace controller