#include "Decoder.h"
#include "ffmpeg_helper/FFmpegError.h"
#ifdef _WIN32
#include "DxvaDecoder.h"
#endif

namespace controller {
std::unique_ptr<Decoder> Decoder::create(const Params& params)
{
    std::unique_ptr<Decoder> decoder{nullptr};
    if (params.type == Type::Dxva) {
#ifdef _WIN32
        decoder = std::unique_ptr<Decoder>(new DxvaDecoder(Type::Dxva)); 
#endif
    } else {
        decoder = std::unique_ptr<Decoder>(new Decoder(Type::Software));
    }
    if (!decoder) {
        if (params.logger) params.logger->error() << "Failed to create decoder. type: " << static_cast<int>(params.type);
        return nullptr;
    }
    if (!decoder->initialize(params)) {
        return nullptr;
    }
    return decoder;
}

Decoder::Decoder(Type type)
    : m_type(type)
{
}

bool Decoder::sendPacket(controller::PacketPtr&& packet)
{
    auto avPacket = packet->avPacket();
    if (packet->type() == Packet::PacketType::End) {
        avPacket = nullptr;
    }
    int ret = avcodec_send_packet(m_codecCtx.get(), avPacket);
    if (ret < 0) {
        LOGE("Failed to send packet: " << fh::err2str(ret));
        return false;
    }
    return true;
}

std::expected<std::unique_ptr<Frame>, int> Decoder::receiveFrame(int serial)
{
    auto frame = Frame::create(serial, IFrame::FrameType::Normal);
    if (!frame) {
        return std::unexpected(-1);
    }
    int ret = avcodec_receive_frame(m_codecCtx.get(), frame->avFrame());
    if (ret < 0) {
        return std::unexpected(ret);
    }
    return frame;
}

void Decoder::flush()
{
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx.get());
    }
}

int Decoder::width() const
{
    return m_codecCtx ? m_codecCtx->width : 0;
}

int Decoder::height() const
{
    return m_codecCtx ? m_codecCtx->height : 0;
}

bool Decoder::initialize(const Params& params)
{
    if (!params.logger)
        return false;
    m_logger = params.logger;
    if (!createContext(params.stream)) {
        return false;
    }
    return openCodec(params.stream);
}

bool Decoder::createContext(const AVStream* stream)
{
    FUNC_TRACE();
    if (!stream) {
        LOGE("Invalid stream");
        return false;
    }
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec){
        LOGE("Failed to find decoder");
        return false;
    }
    m_codecCtx = fh::makeCodecContext(codec);
    if (!m_codecCtx) {
        LOGE("Failed to create codec context");
        return false;
    }
    m_codecCtx->thread_count = 0; // 自适应线程数，0表示自动选择

    int ret = avcodec_parameters_to_context(m_codecCtx.get(), stream->codecpar);
    if (ret < 0) {
        LOGE("Failed to copy codec parameters to context: " << fh::err2str(ret));
        return false;
    }

    return true;
}

bool Decoder::openCodec(const AVStream* stream)
{
    FUNC_TRACE();
    int ret = avcodec_open2(m_codecCtx.get(), m_codecCtx->codec, nullptr);
    if (ret < 0) {
        LOGE("Failed to open codec: " << fh::err2str(ret));
        return false;
    }
    return true;
}

} // namespace controller