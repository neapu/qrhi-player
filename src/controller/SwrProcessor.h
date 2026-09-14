#pragma once
#include "FrameProcessor.h"
#include "ffmpeg_helper/SwrContext.h"

namespace controller {

class SwrProcessor : public FrameProcessor {
public:
    struct TargetFormat {
        AVSampleFormat format{AV_SAMPLE_FMT_NONE}; // AV_SAMPLE_FMT_NONE表示保持原格式
        int sampleRate{0};           // 填0表示保持原采样率
        AVChannelLayout chLayout{};  // nb_channels为0表示保持原声道布局
    };
    SwrProcessor(const TargetFormat& targetFormat);
    ~SwrProcessor() override = default;

    std::unique_ptr<Frame> process(std::unique_ptr<Frame>&& frame, const ProcessorContext& context) override;

private:
    TargetFormat m_targetFormat;
    fh::SwrContextPtr m_swrContext{nullptr};
    AVSampleFormat m_srcFormat{AV_SAMPLE_FMT_NONE};
    int m_srcSampleRate{0};
    AVChannelLayout m_srcChLayout{};
};

} // namespace controller
