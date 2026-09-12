#pragma once
#include <memory>
#include <optional>
#include "ffmpeg_helper/CodecContext.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include "Packet.h"
#include "FrameImpl.h"

namespace controller {
class Decoder {
public:
    static std::unique_ptr<Decoder> create(const AVStream* stream);

    virtual ~Decoder() = default;

    virtual bool sendPacket(controller::PacketPtr&& packet);
    virtual std::optional<std::unique_ptr<Frame>> receiveFrame();

protected:
    Decoder() = default;
    bool initialize(const AVStream* stream);
    bool createContext(const AVStream* stream);
    bool openCodec(const AVStream* stream);

protected:
    fh::CodecContextPtr m_codecCtx{nullptr};
};
using DecoderPtr = std::shared_ptr<Decoder>;
} // namespace controller