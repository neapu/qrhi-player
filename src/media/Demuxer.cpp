#include "Demuxer.h"
#include "FFmpegError.h"

namespace media {
std::unique_ptr<Demuxer> Demuxer::create(const Params& params)
{
    auto demuxer = std::unique_ptr<Demuxer>(new Demuxer(params));
    if (demuxer->initialize()) {
        return demuxer;
    }
    return nullptr;
}

Demuxer::Demuxer(const Params& params)
    : m_url(params.url), m_logger(params.logger)
{
}

bool Demuxer::initialize()
{
    FUNC_TRACE(m_logger, spdlog::level::info);

    auto ret = fh::makeInputContext(m_url.c_str());
    if (!ret) {
        int err = ret.error();
        LOG_ERROR(m_logger, "Failed to create input context, error: {}", fh::err2str(err));
        return false;
    }
    int err = avformat_find_stream_info(ret.value().get(), nullptr);
    if (err < 0) {
        LOG_ERROR(m_logger, "Failed to find stream info, error: {}", fh::err2str(err));
        return false;
    }

    m_inputContext = std::move(ret.value());
    return true;
}

uint32_t Demuxer::streamCount() const
{
    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return 0;
    }
    return m_inputContext->nb_streams;
}

const AVStream* Demuxer::stream(uint32_t index) const
{
    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return nullptr;
    }
    if (index >= m_inputContext->nb_streams) {
        LOG_ERROR(m_logger, "Stream index out of range");
        return nullptr;
    }
    return m_inputContext->streams[index];
}

AVStream* Demuxer::stream(uint32_t index)
{
    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return nullptr;
    }
    if (index >= m_inputContext->nb_streams) {
        LOG_ERROR(m_logger, "Stream index out of range");
        return nullptr;
    }
    return m_inputContext->streams[index];
}

bool Demuxer::seek(int64_t us)
{
    FUNC_TRACE(m_logger, spdlog::level::info);

    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return false;
    }

    auto ret = avformat_seek_file(m_inputContext.get(), -1, INT64_MIN, us, INT64_MAX, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to seek, error: {}", fh::err2str(ret));
        return false;
    }

    return true;
}

bool Demuxer::toHead()
{
    FUNC_TRACE(m_logger, spdlog::level::info);

    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return false;
    }

    auto ret = avformat_seek_file(m_inputContext.get(), -1, INT64_MIN, 0, INT64_MAX, 0);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to seek to head, error: {}", fh::err2str(ret));
        return false;
    }

    return true;
}

int64_t Demuxer::duration() const
{
    FUNC_TRACE(m_logger, spdlog::level::info);

    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return 0;
    }

    if (m_inputContext->duration != AV_NOPTS_VALUE && m_inputContext->duration > 0) {
        return m_inputContext->duration;
    }

    const AVStream* fallback{nullptr};
    for (unsigned int i = 0; i < m_inputContext->nb_streams; ++i) {
        const AVStream* stream = m_inputContext->streams[i];
        if (!stream || !stream->codecpar || stream->duration == AV_NOPTS_VALUE || stream->duration <= 0) {
            continue;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            fallback = stream;
            break;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !fallback) {
            fallback = stream;
        }
    }

    if (fallback) {
        return av_rescale_q(fallback->duration, fallback->time_base, AV_TIME_BASE_Q);
    }

    LOG_WARN(m_logger, "Failed to determine duration from both input context and streams");
    return 0;
}

fh::PacketPtr Demuxer::readPacket()
{
    if (!m_inputContext) {
        LOG_ERROR(m_logger, "Input context is not initialized");
        return nullptr;
    }

    fh::PacketPtr packet = fh::makePacket();
    int ret = av_read_frame(m_inputContext.get(), packet.get());
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            m_endOfFile = true;
            return nullptr;
        }
        LOG_ERROR(m_logger, "Failed to read frame, error: {}", fh::err2str(ret));
        return nullptr;
    }

    return packet;
}

bool Demuxer::endOfFile() const
{
    return m_endOfFile;
}
AVFormatContext* Demuxer::formatContext() const
{
    return m_inputContext.get();
}

} // namespace media