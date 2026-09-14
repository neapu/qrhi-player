#include "Decoder.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
std::unique_ptr<Decoder> Decoder::create(const AVStream* stream, std::shared_ptr<Logger> logger)
{
    auto decoder = std::unique_ptr<Decoder>(new Decoder());
    if (!decoder->initialize(stream, logger))
        return nullptr;
    return decoder;
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

bool Decoder::initialize(const AVStream* stream, std::shared_ptr<Logger> logger)
{
    if (!logger)
        return false;
    m_logger = logger;
    if (!createContext(stream)) {
        return false;
    }
    return openCodec(stream);
}

bool Decoder::createContext(const AVStream* stream)
{
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
    // codecpar中的extradata(MP4的SPS/PPS、AAC的ASC)不会随avcodec_open2自动进入上下文，
    // 缺失时AVCC格式流的每个包都会解码失败；MPEG-TS等在带内传参数集的流不受影响
    int ret = avcodec_parameters_to_context(m_codecCtx.get(), stream->codecpar);
    if (ret < 0) {
        LOGE("Failed to copy codec parameters to context: " << fh::err2str(ret));
        return false;
    }

    return true;
}

bool Decoder::openCodec(const AVStream* stream)
{
    int ret = avcodec_open2(m_codecCtx.get(), m_codecCtx->codec, nullptr);
    if (ret < 0) {
        LOGE("Failed to open codec: " << fh::err2str(ret));
        return false;
    }
    return true;
}

} // namespace controller