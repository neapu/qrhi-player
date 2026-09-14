#include "SwsContext.h"

namespace fh {
void SwsContextDeleter::operator()(SwsContext* ctx) const
{
    if (ctx) {
        sws_freeContext(ctx);
    }
}

SwsContextPtr createSwsContext(int srcW, int srcH, AVPixelFormat srcFormat,
                               int dstW, int dstH, AVPixelFormat dstFormat,
                               int flags)
{
    SwsContext* ctx = sws_getContext(srcW, srcH, srcFormat,
                                     dstW, dstH, dstFormat,
                                     flags, nullptr, nullptr, nullptr);
    return SwsContextPtr(ctx);
}

SwsContextPtr allocateSwsContext()
{
    SwsContext* ctx = sws_alloc_context();
    return SwsContextPtr(ctx);
}

} // namespace fh