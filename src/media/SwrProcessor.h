#pragma once
#include "FrameProcessor.h"
#include "Logger.h"
#include "SwrContext.h"

namespace media {
class SwrProcessor : public FrameProcessor {
public:
    struct Params {
        LoggerPtr logger;
        int sampleRate;
        AVSampleFormat sampleFormat;
        AVChannelLayout channelLayout;
    };

    static std::shared_ptr<SwrProcessor> create(const Params& params);

    fh::FramePtr process(fh::FramePtr&& frame) override;

private:
    explicit SwrProcessor(const Params& params);
    bool initialize(const Params& params);

private:
    LoggerPtr m_logger{nullptr};
    int m_sampleRate{0};
    AVSampleFormat m_sampleFormat{AV_SAMPLE_FMT_NONE};
    AVChannelLayout m_channelLayout{};

    int m_srcSampleRate{0};
    AVSampleFormat m_srcSampleFormat{AV_SAMPLE_FMT_NONE};
    AVChannelLayout m_srcChannelLayout{};

    fh::SwrContextPtr m_swrContext{nullptr};
};

} // namespace media