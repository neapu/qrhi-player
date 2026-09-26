#include "Decoder.h"
#include "FFmpegError.h"

namespace media {
std::unique_ptr<Decoder> Decoder::create(const Params& params)
{
    auto decoder = std::unique_ptr<Decoder>(new Decoder(params));
    if (decoder->initialize()) {
        return decoder;
    }
    return nullptr;
}

Decoder::Decoder(const Params& params)
    : m_type(params.type),
      m_stream(params.stream),
      m_logger(params.logger)
{
}

bool Decoder::initialize()
{
    if (!createContext(m_stream)) {
        return false;
    }
    if (!openCodec(m_stream)) {
        return false;
    }
    return true;
}

bool Decoder::createContext(const AVStream* stream)
{
    if (!stream) {
        return false;
    }
    m_codecCtx = fh::CodecContextPtr(avcodec_alloc_context3(nullptr));
    if (!m_codecCtx) {
        LOG_ERROR(m_logger, "Failed to allocate codec context");
        return false;
    }
    if (int ret = avcodec_parameters_to_context(m_codecCtx.get(), stream->codecpar); ret < 0) {
        LOG_ERROR(m_logger, "Failed to copy codec parameters to context: {}", fh::err2str(ret));
        return false;
    }
    m_codecCtx->time_base = stream->time_base;
    m_codecCtx->pkt_timebase = stream->time_base;
    return true;
}

bool Decoder::openCodec(const AVStream* stream)
{
    if (!stream) {
        return false;
    }

    if (int ret = avcodec_open2(m_codecCtx.get(), m_codecCtx->codec, nullptr); ret < 0) {
        LOG_ERROR(m_logger, "Failed to open codec: {}", fh::err2str(ret));
        return false;
    }
    return true;
}

bool Decoder::sendPacket(fh::PacketPtr&& packet)
{
    int ret = avcodec_send_packet(m_codecCtx.get(), packet.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        LOG_ERROR(m_logger, "Failed to send packet: {}", fh::err2str(ret));
        return false;
    }
    return true;
}

std::expected<fh::FramePtr, int> Decoder::receiveFrame()
{
    auto frame = fh::makeFrame();
    int ret = avcodec_receive_frame(m_codecCtx.get(), frame.get());
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            LOG_ERROR(m_logger, "Failed to receive frame: {}", fh::err2str(ret));
        }
        return std::unexpected(ret);
    }
    return frame;
}

void Decoder::flush()
{
    avcodec_flush_buffers(m_codecCtx.get());
}

int Decoder::width() const
{
    return m_codecCtx ? m_codecCtx->width : 0;
}

int Decoder::height() const
{
    return m_codecCtx ? m_codecCtx->height : 0;
}

AVPixelFormat Decoder::pixelFormat() const
{
    return m_codecCtx ? m_codecCtx->pix_fmt : AV_PIX_FMT_NONE;
}

AVSampleFormat Decoder::sampleFormat() const
{
    return m_codecCtx ? m_codecCtx->sample_fmt : AV_SAMPLE_FMT_NONE;
}

int Decoder::sampleRate() const
{
    return m_codecCtx ? m_codecCtx->sample_rate : 0;
}

const AVChannelLayout& Decoder::chLayout() const
{
    // 未打开或未知声道布局时返回空的静态布局(nb_channels == 0)
    static const AVChannelLayout unspecifiedLayout{};
    return m_codecCtx ? m_codecCtx->ch_layout : unspecifiedLayout;
}

AVRational Decoder::timeBase() const
{
    return m_stream ? m_stream->time_base : AVRational{0, 1};
}

AVRational Decoder::frameRate() const
{
    if (!m_codecCtx) {
        return AVRational{0, 1};
    }
    if (m_codecCtx->framerate.num != 0) {
        return m_codecCtx->framerate;
    }
    if (!m_stream) {
        return AVRational{0, 1};
    }
    return m_stream->r_frame_rate.num != 0 ? m_stream->r_frame_rate : m_stream->avg_frame_rate;
}

} // namespace media