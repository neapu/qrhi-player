#pragma once
#include "ffmpeg_helper/InputContext.h"
#include "FrameImpl.h"
#include "Logger.h"

namespace controller {
class FrameProcessor {
public:
    virtual ~FrameProcessor() = default;

    virtual std::unique_ptr<Frame> process(std::unique_ptr<Frame>&& frame) = 0;
    virtual void flush() {};
};
} // namespace controller