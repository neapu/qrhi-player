#pragma once
#include "ffmpeg_helper/InputContext.h"
#include "FrameImpl.h"
#include "Logger.h"

namespace controller {
struct ProcessorContext {
    const AVStream* stream;
    std::shared_ptr<Logger> logger;
};
class FrameProcessor {
public:
    virtual ~FrameProcessor() = default;

    virtual std::unique_ptr<Frame> process(std::unique_ptr<Frame>&& frame, const ProcessorContext& context) = 0;
    virtual void flush() {};
};
} // namespace controller