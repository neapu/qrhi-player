#pragma once
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace fh {
struct CodecContextDeleter {
    void operator()(AVCodecContext* ctx) const;
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
CodecContextPtr makeCodecContext(const AVCodec* codec);

} // namespace fh