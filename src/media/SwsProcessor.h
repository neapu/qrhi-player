#pragma once
#include "FrameProcessor.h"
#include "Logger.h"
#include "SwsContext.h"

namespace media {
class SwsProcessor : public FrameProcessor {
public:
    struct Params {
        LoggerPtr logger{nullptr};
        AVPixelFormat pixelFormat{AV_PIX_FMT_NONE};
        int width{0};
        int height{0};
    };

    static std::shared_ptr<SwsProcessor> create(const Params& params);
    fh::FramePtr process(fh::FramePtr&& frame) override;

private:
    SwsProcessor(const Params& params);
    bool initialize();

private:
    LoggerPtr m_logger{nullptr};
    int m_width{0};
    int m_height{0};
    AVPixelFormat m_pixelFormat{AV_PIX_FMT_NONE};

    fh::SwsContextPtr m_swsContext{nullptr};
};
} // namespace media