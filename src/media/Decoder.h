#pragma once
#include <memory>
#include <expected>
#include "CodecContext.h"
#include "Frame.h"
#include "Packet.h"
#include "Logger.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include "Packet.h"
#include "Logger.h"

namespace media {
class Decoder {
public:
    enum class Type {
        Software,
        Dxva,
        // Add other types as needed
    };
    struct Params {
        Type type{Type::Software};
        const AVStream* stream{nullptr};
        LoggerPtr logger{nullptr};
#ifdef _WIN32
        // 裸指针安全声明：只借用，不管理生命周期
        // void*类型安全声明：必须由调用者保证类型为 ID3D11Device*
        void* d3d11Device{nullptr};
#endif
    };
    static std::unique_ptr<Decoder> create(const Params& params);

    virtual ~Decoder() = default;

    virtual bool sendPacket(fh::PacketPtr&& packet);
    virtual std::expected<fh::FramePtr, int> receiveFrame();

    virtual void flush();

    // 已打开的解码上下文：输出帧的采样率/声道等以此为准(create成功后有效)
    const AVCodecContext* codecContext() const { return m_codecCtx.get(); }

    Type type() const { return m_type; }

    int width() const;
    int height() const;

    const AVStream* stream() const { return m_stream; }

    AVPixelFormat pixelFormat() const;
    AVSampleFormat sampleFormat() const;

    int sampleRate() const;
    const AVChannelLayout& chLayout() const;
    AVRational timeBase() const;
    AVRational frameRate() const;

protected:
    explicit Decoder(const Params& params);
    virtual bool initialize();
    virtual bool createContext(const AVStream* stream);
    virtual bool openCodec(const AVStream* stream);

protected:
    Type m_type{Type::Software};
    const AVStream* m_stream{nullptr};
    LoggerPtr m_logger{nullptr};
    fh::CodecContextPtr m_codecCtx{nullptr};
};
using DecoderPtr = std::unique_ptr<Decoder>;
} // namespace media