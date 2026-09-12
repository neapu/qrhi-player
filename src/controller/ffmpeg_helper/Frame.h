#pragma once
#include <memory>

extern "C" {
#include <libavutil/frame.h>
}

namespace fh {
struct FrameDeleter {
    void operator()(AVFrame* frame) const;
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
FramePtr makeFrame();

} // namespace fh