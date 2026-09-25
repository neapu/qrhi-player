#include "SwsProcessor.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace media {
std::shared_ptr<SwsProcessor> SwsProcessor::create(const Params& params)
{
    auto processor = std::shared_ptr<SwsProcessor>(new SwsProcessor(params));
    if (!processor->initialize()) {
        return nullptr;
    }
    return processor;
}
SwsProcessor::SwsProcessor(const Params& params)
    : m_logger(params.logger),
      m_width(params.width),
      m_height(params.height),
      m_pixelFormat(params.pixelFormat)
{
}

bool SwsProcessor::initialize()
{
    if (m_width <= 0 || m_height <= 0 || m_pixelFormat == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "Invalid SwsProcessor parameters: width={}, height={}, pixelFormat={}", m_width, m_height, static_cast<int>(m_pixelFormat));
        return false;
    }

    m_swsContext = fh::allocateSwsContext();
    if (!m_swsContext) {
        LOG_ERROR(m_logger, "Failed to allocate SwsContext");
        return false;
    }
    m_swsContext->flags = SWS_BILINEAR;
    // 启用多线程 ffmpeg version >= 7.1
    int ret = av_opt_set_int(m_swsContext.get(), "threads", 0, 0);
    if (ret < 0) {
        LOG_WARN(m_logger, "Failed to set SwsContext threads");
    }
    return true;
}

fh::FramePtr SwsProcessor::process(fh::FramePtr&& frame)
{
    if (!frame) {
        return nullptr;
    }
    if (frame->format == m_pixelFormat
        && frame->width == m_width
        && frame->height == m_height) {
        return std::move(frame);
    }
    
    if (!m_swsContext) {
        LOG_ERROR(m_logger, "SwsContext is not initialized");
        return nullptr;
    }

    auto dstFrame = fh::makeFrame();
    if (!dstFrame) {
        LOG_ERROR(m_logger, "Failed to allocate destination frame");
        return nullptr;
    }
    dstFrame->width = m_width;
    dstFrame->height = m_height;
    dstFrame->format = m_pixelFormat;

    int ret = sws_scale_frame(m_swsContext.get(), dstFrame.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to scale frame");
        return nullptr;
    }
    av_frame_copy_props(dstFrame.get(), frame.get());

    return std::move(dstFrame);
}

} // namespace media