#include "Frame.h"

namespace fh {

void FrameDeleter::operator()(AVFrame* frame) const
{
    if (frame) {
        av_frame_free(&frame);
    }
}

FramePtr makeFrame()
{
    AVFrame* frame = av_frame_alloc();
    return FramePtr(frame);
}

} // namespace fh