#pragma once
#include <memory>

extern "C" {
#include <libswscale/swscale.h>
}

namespace fh {
struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const;
};

using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

SwsContextPtr createSwsContext(int srcW, int srcH, AVPixelFormat srcFormat,
                               int dstW, int dstH, AVPixelFormat dstFormat,
                               int flags);
SwsContextPtr allocateSwsContext();

} // namespace fh