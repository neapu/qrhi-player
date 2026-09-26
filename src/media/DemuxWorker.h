#pragma once
#include "Demuxer.h"
#include "PacketQueue.h"

namespace media {
class DemuxWorker {
public:
    struct Params {
        LoggerPtr logger;
        // 一般将音频流设为主流
        // 这一路流不会被丢包，队列满时会阻塞解封装线程，用于控制读取文件的速度
        // 非主流在队列满时会丢弃最旧的包
        uint32_t mainStreamIndex{0};
    };

    static std::unique_ptr<DemuxWorker> create(const Params& params, DemuxerPtr&& demuxer);

    // 调用约定，整个生命周期只能调用一次start和stop，停止后不能再次启动
    void start();
    void stop();

    // 仅允许在 start 之前调用：修改主流，各队列的丢包策略随之重建
    void setMainStream(uint32_t streamIndex);

    fh::PacketPtr nextPacket(uint32_t streamIndex);
    void seek(int64_t us);

    bool streamQueueEmpty(uint32_t streamIndex) const;
    bool endOfFile() const { return m_endOfFile; }
private:
    DemuxWorker(const Params& params, DemuxerPtr&& demuxer);
    bool initialize();

    // 按当前主流为每条流创建 PacketQueue（主流阻塞式，其余满时丢最旧包）
    void createPacketQueues();

    void workerThread();

    void interruptMainStreamQueue();
    void clearAllPacketQueues();

private:
    LoggerPtr m_logger{nullptr};
    uint32_t m_mainStreamIndex{0};
    DemuxerPtr m_demuxer{nullptr};
    std::thread m_workerThread{};
    std::atomic_bool m_exitFlag{false};
    bool m_started{false};
    std::unordered_map<uint32_t, PacketQueue> m_packetQueues{};
    std::atomic_bool m_endOfFile{false};

    int64_t m_seekTargetUs{AV_NOPTS_VALUE};
    std::mutex m_seekMutex{};
    std::condition_variable m_seekCV{};
    int m_serial{0};
};
using DemuxWorkerPtr = std::unique_ptr<DemuxWorker>;

} // namespace media