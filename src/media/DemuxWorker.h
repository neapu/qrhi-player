#pragma once
#include <unordered_map>
#include "Demuxer.h"
#include "Logger.h"
#include "MediaPacket.h"
#include "PacketQueue.h"

namespace media {
class DemuxWorker {
public:
    struct Params {
        LoggerPtr logger;
        std::function<void(bool, int)> onSeekCompleted;
    };
    static std::unique_ptr<DemuxWorker> create(const Params& params, DemuxerPtr&& demuxer);

    // 创建指定流的包队列，缓存指定容量的媒体包。
    // 必须在 start 之前创建，start 之后不能再调用
    void createPacketQueue(int streamIndex, size_t capacity);

    // 启动工作线程，对象生命周期只能启动一次，停止后不能再启动
    void start();
    void stop();

    void seek(int64_t positionUs);

    MediaPacket popPacket(int streamIndex);
private:
    DemuxWorker(const Params& params, DemuxerPtr&& demuxer);
    bool initialize();

    void workerThread();

    void stopAllPacketQueues();
    void interruptAllPacketQueues();
    void clearAllPacketQueues();
    void pushFlashPacketToAllQueues();

private:
    DemuxerPtr m_demuxer{nullptr};
    LoggerPtr m_logger{nullptr};
    std::function<void(bool, int)> m_onSeekCompleted{nullptr};

    std::unordered_map<int, std::unique_ptr<PacketQueue>> m_packetQueues{};
    std::thread m_workerThread{};
    std::atomic_bool m_exitFlag{false};

    int m_serial{0};
    std::atomic_int64_t m_seekPositionUs{AV_NOPTS_VALUE};
};

} // namespace media