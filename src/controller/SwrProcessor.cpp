#include "SwrProcessor.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
SwrProcessor::SwrProcessor(const TargetFormat& targetFormat, std::shared_ptr<Logger> logger)
    : m_targetFormat(targetFormat), m_logger(std::move(logger))
{
}

std::unique_ptr<Frame> SwrProcessor::process(std::unique_ptr<Frame>&& frame)
{
    if (!frame) return nullptr;
    auto* avFrame = frame->avFrame();
    if (avFrame == nullptr) return nullptr;

    int targetSampleRate = m_targetFormat.sampleRate == 0 ? avFrame->sample_rate : m_targetFormat.sampleRate;
    const AVChannelLayout& targetLayout = m_targetFormat.chLayout.nb_channels == 0
        ? avFrame->ch_layout
        : m_targetFormat.chLayout;
    AVSampleFormat targetFormat = m_targetFormat.format == AV_SAMPLE_FMT_NONE
        ? static_cast<AVSampleFormat>(avFrame->format)
        : m_targetFormat.format;

    if (avFrame->format == targetFormat
        && avFrame->sample_rate == targetSampleRate
        && av_channel_layout_compare(&avFrame->ch_layout, &targetLayout) == 0) {
        return frame;
    }

    // 输入参数变化时重建上下文
    if (!m_swrContext
        || avFrame->format != m_srcFormat
        || avFrame->sample_rate != m_srcSampleRate
        || av_channel_layout_compare(&avFrame->ch_layout, &m_srcChLayout) != 0) {
        m_swrContext = fh::createSwrContext(&targetLayout, targetFormat, targetSampleRate,
                                            &avFrame->ch_layout, static_cast<AVSampleFormat>(avFrame->format), avFrame->sample_rate);
        if (!m_swrContext) {
            LOGE("Failed to create SwrContext");
            return nullptr;
        }
        m_srcFormat = static_cast<AVSampleFormat>(avFrame->format);
        m_srcSampleRate = avFrame->sample_rate;
        m_srcChLayout = avFrame->ch_layout;
    }

    auto dstFrame = Frame::create(frame->serial(), frame->type());
    if (!dstFrame) {
        LOGE("Failed to create destination frame");
        return nullptr;
    }
    auto* dstAVFrame = dstFrame->avFrame();
    // swr_convert_frame要求输出帧的格式/采样率/声道布局已设置，不匹配时返回AVERROR_OUTPUT_CHANGED
    dstAVFrame->format = targetFormat;
    dstAVFrame->sample_rate = targetSampleRate;
    dstAVFrame->ch_layout = targetLayout;

    int ret = swr_convert_frame(m_swrContext.get(), dstAVFrame, avFrame);
    if (ret < 0) {
        LOGE("Failed to convert frame: " << fh::err2str(ret));
        return nullptr;
    }
    // pts直接沿用输入帧的数值，未补偿重采样延迟（首帧会有轻微提前）
    av_frame_copy_props(dstAVFrame, avFrame);

    return dstFrame;
}
} // namespace controller
