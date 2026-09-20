#include "SwsProcessor.h"
#include "ffmpeg_helper/FFmpegError.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

namespace controller {
SwsProcessor::SwsProcessor(const TargetFormat& targetFormat, std::shared_ptr<Logger> logger)
    : m_targetFormat(targetFormat), m_logger(std::move(logger))
{
    // dynamic 模式，每次转换都由 sws_scale_frame 动态创建转换视图，不用调用 sws_init_context
    m_swsContext = fh::allocateSwsContext();
    if (!m_swsContext) {
        LOGE("Failed to allocate SwsContext");
        return;
    }
    m_swsContext->flags = SWS_BILINEAR;
    // 启用多线程
    av_opt_set_int(m_swsContext.get(), "threads", 0, 0);
}

std::unique_ptr<Frame> SwsProcessor::process(std::unique_ptr<Frame>&& frame)
{
    if (!frame) return nullptr;
    auto* avFrame = frame->avFrame();
    if (avFrame == nullptr) return nullptr;

    if (!m_swsContext) {
        LOGE("SwsContext is not initialized");
        return nullptr;
    }

    int targetWidth = m_targetFormat.width == 0 ? avFrame->width : m_targetFormat.width;
    int targetHeight = m_targetFormat.height == 0 ? avFrame->height : m_targetFormat.height;
    AVPixelFormat targetFormat = m_targetFormat.format == AV_PIX_FMT_NONE
        ? static_cast<AVPixelFormat>(avFrame->format)
        : m_targetFormat.format;

    if (avFrame->format == targetFormat
        && avFrame->width == targetWidth
        && avFrame->height == targetHeight) {
        return frame;
    }

    auto dstFrame = Frame::create(frame->serial(), frame->type());
    if (!dstFrame) {
        LOGE("Failed to create destination frame");
        return nullptr;
    }
    auto* dstAVFrame = dstFrame->avFrame();
    dstAVFrame->format = targetFormat;
    dstAVFrame->width = targetWidth;
    dstAVFrame->height = targetHeight;

    int ret = sws_scale_frame(m_swsContext.get(), dstAVFrame, avFrame);
    if (ret < 0) {
        LOGE("Failed to scale frame: " << fh::err2str(ret));
        return nullptr;
    }
    av_frame_copy_props(dstAVFrame, avFrame);

    return dstFrame;
}
} // namespace controller