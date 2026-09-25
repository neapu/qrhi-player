#include "SwrProcessor.h"

namespace media {
std::shared_ptr<SwrProcessor> SwrProcessor::create(const Params& params)
{
    auto processor = std::shared_ptr<SwrProcessor>(new SwrProcessor(params));
    if (processor->initialize(params)) {
        return processor;
    }
    return nullptr;
}

SwrProcessor::SwrProcessor(const Params& params)
    : m_logger(params.logger),
      m_sampleRate(params.sampleRate),
      m_sampleFormat(params.sampleFormat),
      m_channelLayout{}
{
    av_channel_layout_copy(&m_channelLayout, &params.channelLayout);
}

bool SwrProcessor::initialize(const Params& params)
{
    if (m_sampleRate <= 0 || m_sampleFormat == AV_SAMPLE_FMT_NONE || m_channelLayout.nb_channels <= 0) {
        return false;
    }
    return true;
}

fh::FramePtr SwrProcessor::process(fh::FramePtr&& frame)
{
    if (!frame) {
        return nullptr;
    }

    if (frame->sample_rate == m_sampleRate
        && frame->format == m_sampleFormat
        && av_channel_layout_compare(&frame->ch_layout, &m_channelLayout) == 0) {
        return std::move(frame);
    }

    if (!m_swrContext
        || m_srcSampleRate != frame->sample_rate
        || m_srcSampleFormat != frame->format
        || av_channel_layout_compare(&m_srcChannelLayout, &frame->ch_layout) != 0) {
        m_srcSampleRate = frame->sample_rate;
        m_srcSampleFormat = static_cast<AVSampleFormat>(frame->format);
        av_channel_layout_copy(&m_srcChannelLayout, &frame->ch_layout);
        m_swrContext = fh::createSwrContext(&m_channelLayout, m_sampleFormat, m_sampleRate,
            &m_srcChannelLayout, m_srcSampleFormat, m_srcSampleRate);
    }
    if (!m_swrContext) {
        LOG_ERROR(m_logger, "Failed to create SwrContext");
        return nullptr;
    }

    auto dstFrame = fh::makeFrame();
    if (!dstFrame) {
        LOG_ERROR(m_logger, "Failed to create destination frame");
        return nullptr;
    }
    dstFrame->format = m_sampleFormat;
    dstFrame->sample_rate = m_sampleRate;
    av_channel_layout_copy(&dstFrame->ch_layout, &m_channelLayout);

    int ret = swr_convert_frame(m_swrContext.get(), dstFrame.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to convert frame");
        return nullptr;
    }
    av_frame_copy_props(dstFrame.get(), frame.get());

    return std::move(dstFrame);
}

} // namespace media