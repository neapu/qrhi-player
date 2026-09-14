#pragma once
#include "controller/Controller.h"
#include "Logger.h"
#include "DemuxerWorker.h"
#include "DecodeWorker.h"

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
    void onPacketRead(controller::PacketPtr&& packet);

private:
    Params m_params;
    std::shared_ptr<Logger> m_logger{};
    std::unique_ptr<DemuxerWorker> m_demuxerWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_videoWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_audioWorker{nullptr};
};

} // namespace controller