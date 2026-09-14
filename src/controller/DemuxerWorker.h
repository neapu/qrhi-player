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
        std::string url;
        std::shared_ptr<Logger> logger;
        std::function<void(controller::PacketPtr)> onPacketRead;
    };
    static std::unique_ptr<DemuxerWorker> create(const Params& params);

    ~DemuxerWorker();

    int serial() const { return m_serial.load(); }
    void seek(int streamIndex, int64_t pts);

    double duration() const;

    // 调用约定：整个生命周期只能调用一次start()，stop()用于停止线程，停止后不能再启动
    void start();
    void stop();

    std::unique_ptr<Demuxer>& demuxer() { return m_demuxer; }
private:
    DemuxerWorker() = default;
    bool initialize(const Params& params);

    void workerFunc();

private:
    std::shared_ptr<Logger> m_logger{nullptr};
    std::function<void(controller::PacketPtr)> m_onPacketRead;
    std::unique_ptr<Demuxer> m_demuxer;
    std::thread m_thread;
    std::atomic_bool m_exitFlag{false};
    std::atomic_int m_serial{0};

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