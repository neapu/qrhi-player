#pragma once
#include <memory>
#include <expected>
#include "ffmpeg_helper/CodecContext.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include "Packet.h"
#include "FrameImpl.h"
#include "Logger.h"

namespace controller {
class Decoder {
public:
    static std::unique_ptr<Decoder> create(const AVStream* stream, std::shared_ptr<Logger> logger);

    virtual ~Decoder() = default;

    virtual bool sendPacket(controller::PacketPtr&& packet);
    virtual std::expected<std::unique_ptr<Frame>, int> receiveFrame(int serial);

    virtual void flush();

    // 已打开的解码上下文：输出帧的采样率/声道等以此为准(create成功后有效)
    const AVCodecContext* codecContext() const { return m_codecCtx.get(); }

protected:
    Decoder() = default;
    bool initialize(const AVStream* stream, std::shared_ptr<Logger> logger);
    bool createContext(const AVStream* stream);
    bool openCodec(const AVStream* stream);

protected:
    std::shared_ptr<Logger> m_logger{nullptr};
    fh::CodecContextPtr m_codecCtx{nullptr};
};
using DecoderPtr = std::shared_ptr<Decoder>;
} // namespace controller