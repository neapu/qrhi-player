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
    virtual void audioRenderTime(int64_t renderTimeUs) override;
    virtual double duration() const override;
    virtual void seek(double timepoint) override;
    virtual void pauseOrResume() override;
    virtual bool isPaused() const override;

private:
    void onPacketRead(controller::PacketPtr&& packet);
    int64_t clockUsLocked() const; // 读取当前媒体时间(us)，调用方需持有m_clock.mutex
    void reanchorLocked(int64_t mediaUs, double speed); // 重新锚定时钟并设置速率，调用方需持有m_clock.mutex

private:
    Params m_params;
    std::shared_ptr<Logger> m_logger{};
    std::unique_ptr<DemuxerWorker> m_demuxerWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_videoWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_audioWorker{nullptr};

    struct Clock {
        int64_t anchorWallUs{0};  // 锚点：墙上时钟(steady_clock)时间(us)
        int64_t anchorMediaUs{0}; // 锚点：媒体时间(us)
        int64_t pausedMediaUs{0}; // 暂停时冻结的媒体时间(us)
        bool paused{false};
        double speed{1.0}; // 时钟速率：音画同步中等偏差时以略快/略慢的速率平滑收敛，保证时钟连续
        mutable std::mutex mutex;
    };

    Clock m_clock;
};

} // namespace controller