#include "HWTransferProcessor.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
HWTransferProcessor::HWTransferProcessor(std::shared_ptr<Logger> logger)
    : m_logger(std::move(logger))
{
}

std::unique_ptr<Frame> HWTransferProcessor::process(std::unique_ptr<Frame>&& frame)
{
    if (!frame) {
        return nullptr;
    }
    if (!frame->avFrame()->hw_frames_ctx) {
        LOGW("Frame is not a hardware frame");
        return std::move(frame);
    }
    auto swFrame = Frame::create(frame->serial(), frame->type());
    int ret = av_hwframe_transfer_data(swFrame->avFrame(), frame->avFrame(), 0);
    if (ret < 0) {
        LOGE("Failed to transfer hardware frame data: " << fh::err2str(ret));
        return nullptr;
    }
    av_frame_copy_props(swFrame->avFrame(), frame->avFrame());
    return swFrame;
}

} // namespace controller