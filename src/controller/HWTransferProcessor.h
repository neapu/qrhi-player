#pragma once
#include "FrameProcessor.h"

namespace controller {

class HWTransferProcessor : public FrameProcessor {
public:
    HWTransferProcessor(std::shared_ptr<Logger> logger);
    ~HWTransferProcessor() override = default;

    std::unique_ptr<Frame> process(std::unique_ptr<Frame>&& frame) override;
private:
    std::shared_ptr<Logger> m_logger;
};

} // namespace controller