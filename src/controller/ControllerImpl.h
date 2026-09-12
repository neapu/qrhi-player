#pragma once
#include "controller/Controller.h"
#include "Logger.h"

namespace controller {
class Controller final : public IController {
public:
    explicit Controller(const Params& params);
    ~Controller();

    bool initialize();

    virtual FramePtr nextVideoFrame() const override;
    virtual FramePtr nextAudioFrame() const override;
    virtual double duration() const override;
    virtual void seek(double timepoint) override;
    virtual void pauseOrResume() override;
    virtual bool isPaused() const override;
private:
    Params m_params;
    std::shared_ptr<Logger> m_logger{};
};

} // namespace controller