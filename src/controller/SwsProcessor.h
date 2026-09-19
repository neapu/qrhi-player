#pragma once
#include "FrameProcessor.h"
#include "ffmpeg_helper/SwsContext.h"

namespace controller {

class SwsProcessor : public FrameProcessor {
public:
    struct TargetFormat {
        AVPixelFormat format{AV_PIX_FMT_NONE}; // AV_PIX_FMT_NONE表示保持原格式
        int width{0};  // 填0表示保持原宽
        int height{0}; // 填0表示保持原高
    };
    SwsProcessor(const TargetFormat& targetFormat, std::shared_ptr<Logger> logger);
    ~SwsProcessor() override = default;

    std::unique_ptr<Frame> process(std::unique_ptr<Frame>&& frame) override;

private:
    std::shared_ptr<Logger> m_logger;
    TargetFormat m_targetFormat;
    fh::SwsContextPtr m_swsContext{nullptr};
};

} // namespace controller