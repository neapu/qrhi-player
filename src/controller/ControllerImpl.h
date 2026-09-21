#pragma once
#include "controller/Controller.h"
#include "Logger.h"
#include "DemuxerWorker.h"
#include "DecodeWorker.h"
#include "PacketQueue.h"
#include "FrameQueue.h"

namespace controller {
class Controller final : public IController {
public:
    explicit Controller(const Params& params);
    ~Controller();

    bool initialize();

    virtual FramePtr nextVideoFrame() override;
    virtual FramePtr nextAudioFrame() override;
    virtual void audioRenderTime(int64_t renderTimeUs) override;
    virtual std::optional<AudioParams> audioParams() const override;
    virtual double duration() const override;
    virtual double position() const override;
    virtual void seek(double timepoint) override;
    virtual void pauseOrResume() override;
    virtual bool isPaused() const override;
    virtual StatisticsData statistics() const override;

private:
    void onPacketRead(controller::PacketPtr&& packet);
    // 把包投递给指定流的包队列；队列满而等待被seek打断时，重判过时性后决定丢弃或重投
    void routePacket(PacketQueue& packetQueue, controller::PacketPtr&& packet);
    // seek成功后由解封装线程回调：两个队列换代，时钟重锚到本次请求的目标
    void onSeekSucceeded(int serial, int64_t targetUs);
    int64_t clockUsLocked() const; // 读取当前媒体时间(us)，调用方需持有m_clock.mutex
    void reanchorLocked(int64_t mediaUs, double speed); // 重新锚定时钟并设置速率，调用方需持有m_clock.mutex
    DecoderPtr createVideoDecoder(const AVStream* stream, DemuxerPtr& demuxer);
    bool testHardwareDecoder(DecoderPtr& decoder, DemuxerPtr& demuxer, int streamIndex);
    std::optional<FrameProcessorList> makeFrameProcessors(const AVStream* stream, DecoderPtr& decoder);
    void initializeLogger();
    bool initializeVideo(DemuxerPtr& demuxer, int streamIndex);
    bool initializeAudio(DemuxerPtr& demuxer, int streamIndex);

private:
    Params m_params;
    std::shared_ptr<Logger> m_logger{};
    // 队列声明在worker之前：worker只借用队列(裸指针)，析构时先stop worker再由本对象销毁队列
    std::unique_ptr<PacketQueue> m_videoPacketQueue{nullptr};
    std::unique_ptr<PacketQueue> m_audioPacketQueue{nullptr};
    std::unique_ptr<FrameQueue> m_videoFrameQueue{nullptr};
    std::unique_ptr<FrameQueue> m_audioFrameQueue{nullptr};
    std::unique_ptr<DemuxerWorker> m_demuxerWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_videoWorker{nullptr};
    std::unique_ptr<DecodeWorker> m_audioWorker{nullptr};
    // 仅在initialize()内写入、之后只读，无需加锁
    std::optional<AudioParams> m_audioParams{};

    struct Clock {
        // 时钟是跨线程共享的可变状态，线程安全由mutex保证；
        // mutex声明为mutable是因为isPaused()等纯查询接口也要加锁
        int64_t anchorWallUs{0};  // 锚点：墙上时钟(steady_clock)时间(us)
        int64_t anchorMediaUs{0}; // 锚点：媒体时间(us)
        int64_t pausedMediaUs{0}; // 暂停时冻结的媒体时间(us)
        bool paused{false};
        double speed{1.0}; // 时钟速率：音画同步中等偏差时以略快/略慢的速率平滑收敛，保证时钟连续
        // seek过渡期：音频设备缓冲仍在播旧段，其上报会snap回旧位置，忽略直到上报接近时钟(新段实际发声)
        bool suppressAudioSync{false};
        // 最近下发给渲染侧的音频帧serial，seek后首个新serial帧到达时按实际落点重锚
        int servedAudioSerial{-1};
        mutable std::mutex mutex;
    };

    Clock m_clock;
};

} // namespace controller