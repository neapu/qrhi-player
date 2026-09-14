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

double Demuxer::duration() const
{
    if (!m_inputContext) {
        LOGW("Input context is not initialized");
        return 0.0;
    }

    // Prefer the duration recorded in the container (in AV_TIME_BASE units).
    if (m_inputContext->duration != AV_NOPTS_VALUE && m_inputContext->duration > 0) {
        return static_cast<double>(m_inputContext->duration) / AV_TIME_BASE;
    }

    // Fall back to the stream duration, preferring video over audio.
    const AVStream* fallback = nullptr;
    for (unsigned int i = 0; i < m_inputContext->nb_streams; ++i) {
        const AVStream* s = m_inputContext->streams[i];
        if (!s || !s->codecpar || s->duration == AV_NOPTS_VALUE || s->duration <= 0) {
            continue;
        }
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            fallback = s;
            break;
        }
        if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !fallback) {
            fallback = s;
        }
    }

    if (fallback) {
        return static_cast<double>(fallback->duration) * av_q2d(fallback->time_base);
    }

    LOGW("Duration is unknown for both container and streams");
    return 0.0;
}

} // namespace controller