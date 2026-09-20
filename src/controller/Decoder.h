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
    enum class Type {
        Software,
        Dxva,
        // Add other types as needed
    };
    struct Params {
        Type type{Type::Software};
        // 裸指针安全声明：只借用，不管理生命周期
        const AVStream* stream{nullptr};
        std::shared_ptr<Logger> logger{nullptr};
#ifdef _WIN32
        // 裸指针安全声明：只借用，不管理生命周期
        // void*类型安全声明：必须由调用者保证类型为 ID3D11Device*
        void* d3d11Device{nullptr};
#endif
    };
    static std::unique_ptr<Decoder> create(const Params& params);

    virtual ~Decoder() = default;

    virtual bool sendPacket(controller::PacketPtr&& packet);
    virtual std::expected<std::unique_ptr<Frame>, int> receiveFrame(int serial);

    virtual void flush();

    // 已打开的解码上下文：输出帧的采样率/声道等以此为准(create成功后有效)
    const AVCodecContext* codecContext() const { return m_codecCtx.get(); }

    Type type() const { return m_type; }

    int width() const;
    int height() const;

protected:
    explicit Decoder(Type type);
    virtual bool initialize(const Params& params);
    virtual bool createContext(const AVStream* stream);
    virtual bool openCodec(const AVStream* stream);

protected:
    Type m_type{Type::Software};
    std::shared_ptr<Logger> m_logger{nullptr};
    fh::CodecContextPtr m_codecCtx{nullptr};
};
using DecoderPtr = std::unique_ptr<Decoder>;
} // namespace controller