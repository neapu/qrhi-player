#include "SwrContext.h"

namespace fh {
void SwrContextDeleter::operator()(struct SwrContext* ctx) const
{
    if (ctx) {
        swr_free(&ctx);
    }
}

SwrContextPtr createSwrContext(
    const AVChannelLayout* outLayout, enum AVSampleFormat outFormat, int outSampleRate,
    const AVChannelLayout* inLayout, enum AVSampleFormat inFormat, int inSampleRate
) {
    SwrContext* ctx{nullptr};
    int ret = swr_alloc_set_opts2(&ctx,
        outLayout, outFormat, outSampleRate,
        inLayout, inFormat, inSampleRate,
        0, nullptr
    );
    if (ret < 0) {
        if (ctx) {
            swr_free(&ctx);
        }
        return nullptr;
    }
    if (swr_init(ctx) < 0) {
        swr_free(&ctx);
        return nullptr;
    }
    return SwrContextPtr(ctx);
}

} // namespace fh