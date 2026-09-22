#include "CodecContext.h"

namespace fh {
void CodecContextDeleter::operator()(AVCodecContext* ctx) const
{
    if (ctx) {
        avcodec_free_context(&ctx);
    }
}

CodecContextPtr makeCodecContext(const AVCodec* codec)
{
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    return CodecContextPtr(ctx);
}

} // namespace fh