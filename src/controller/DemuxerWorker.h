#pragma once
#include <memory>
#include <string>
#include <thread>
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

    int serial() const { return m_serial.load(); }
    void seek(int streamIndex, int64_t pts);

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
    std::atomic<SeekRequest> m_seekRequest;
};

} // namespace controller