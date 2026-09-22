#pragma once
#include <memory>

extern "C" {
#include <libswresample/swresample.h>
}

namespace fh {
struct SwrContextDeleter {
    void operator()(struct SwrContext* ctx) const;
};

using SwrContextPtr = std::unique_ptr<struct SwrContext, SwrContextDeleter>;
SwrContextPtr createSwrContext(
    const AVChannelLayout* outLayout, enum AVSampleFormat outFormat, int outSampleRate,
    const AVChannelLayout* inLayout, enum AVSampleFormat inFormat, int inSampleRate
);


} // namespace fh