#include "Decoder.h"

namespace controller {
std::unique_ptr<Decoder> Decoder::create(const AVStream* stream)
{
    auto decoder = std::unique_ptr<Decoder>(new Decoder());
    if (!decoder->initialize(stream))
        return nullptr;
    return decoder;
}

bool Decoder::sendPacket(controller::PacketPtr&& packet)
{
    // Implementation goes here
    return false;
}

std::optional<std::unique_ptr<Frame>> Decoder::receiveFrame()
{
    // Implementation goes here
    return std::nullopt;
}

bool Decoder::initialize(const AVStream* stream)
{
    if (!createContext(stream))
        return false;
    return openCodec(stream);
}

bool Decoder::createContext(const AVStream* stream)
{
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec){
        
    }
    m_codecCtx = fh::makeCodecContext(codec);
    if (!m_codecCtx)
        return false;

    return true;
}

bool Decoder::openCodec(const AVStream* stream)
{
    // Implementation goes here
    return false;
}

} // namespace controller