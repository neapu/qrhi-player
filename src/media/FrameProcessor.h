#pragma once
#include "Frame.h"
#include <vector>
#include <memory>

namespace media {
class FrameProcessor {
public:
    virtual ~FrameProcessor() = default;

    virtual fh::FramePtr process(fh::FramePtr&& frame) = 0;
};
using FrameProcessorList = std::vector<std::shared_ptr<FrameProcessor>>;
} // namespace media