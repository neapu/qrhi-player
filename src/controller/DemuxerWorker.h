#pragma once
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "Logger.h"
#include "Demuxer.h"

namespace controller {
class DemuxerWorker {
public:
    struct Params {
        std::shared_ptr<Logger> logger;
        std::function<void(controller::PacketPtr)> onPacketRead;
        // seek成功(serial递增后)在解封装线程上回调，pts为本次消费的请求目标(AV_TIME_BASE单位)。
        // 目标值随请求携带而非读取共享状态，连续seek时每次回调锚定各自的目标
        std::function<void(int newSerial, int64_t pts)> onSeekSucceeded;
    };
    static std::unique_ptr<DemuxerWorker> create(const Params& params, DemuxerPtr&& demuxer);

    ~DemuxerWorker();

    int serial() const { return m_serial.load(); }
    void seek(int streamIndex, int64_t pts);
    // seek请求已投递但尚未被workerFunc消费。供解码侧阻塞谓词查询以丢弃注定过时的包
    bool seekPending() const { return m_seekPending.load(); }

    double duration() const;

    // 调用约定：整个生命周期只能调用一次stop()用于停止线程，停止后不能再启动
    void stop();

    std::unique_ptr<Demuxer>& demuxer() { return m_demuxer; }
private:
    DemuxerWorker() = default;
    bool initialize(const Params& params, DemuxerPtr&& demuxer);

    void workerFunc();

private:
    std::shared_ptr<Logger> m_logger{nullptr};
    std::function<void(controller::PacketPtr)> m_onPacketRead;
    std::function<void(int newSerial, int64_t pts)> m_onSeekSucceeded;
    std::unique_ptr<Demuxer> m_demuxer;
    std::thread m_thread;
    std::atomic_bool m_exitFlag{false};
    std::atomic_int m_serial{0};
    // seek请求已投递未消费。与m_seekRequest同时修改但需被解码线程无锁查询，故为atomic
    std::atomic_bool m_seekPending{false};

    struct SeekRequest {
        int streamIndex{0};
        int64_t pts{AV_NOPTS_VALUE};
    };

    // EOF后workerFunc阻塞在m_seekCV上等待seek请求或退出。
    // m_seekRequest的所有读写都在m_seekMutex内进行：
    // 修改条件变量谓词依赖的状态必须在持锁时完成，否则存在丢唤醒窗口，
    // 因此不需要std::atomic
    std::mutex m_seekMutex;
    std::condition_variable m_seekCV;
    SeekRequest m_seekRequest;
};

} // namespace controller