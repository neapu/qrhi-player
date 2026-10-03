#pragma once
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include "Demuxer.h"
#include "Logger.h"
#include "MediaPacket.h"
#include "PacketQueue.h"

namespace media {
class DemuxWorker {
public:
    enum class Error {
        ReadFailed
    };

    struct Params {
        LoggerPtr logger;
        // 调用约定：回调运行在解封装线程，不允许调用 stop 或析构对象
        std::function<void(bool, int)> onSeekCompleted;
        // 调用约定：回调运行在解封装线程，不允许调用 stop 或析构对象
        std::function<void(Error)> onError;
    };
    static std::unique_ptr<DemuxWorker> create(const Params& params, DemuxerPtr&& demuxer);

    ~DemuxWorker();

    // 创建指定流的包队列，缓存指定容量的媒体包。
    // 没有消费端的流不要创建队列，避免卡死解封装线程
    // 必须在 start 之前创建，start 之后不能再调用
    void createPacketQueue(int streamIndex, size_t capacity);

    // 调用约定：启动工作线程，对象生命周期只能启动一次，停止后不能再启动
    void start();
    // 调用约定：不能在回调中调用 stop
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
    void pushEndPacketToAllQueues();
    void cancelInterruptAllPacketQueues();

private:
    DemuxerPtr m_demuxer{nullptr};
    LoggerPtr m_logger{nullptr};
    std::function<void(bool, int)> m_onSeekCompleted{nullptr};
    std::function<void(Error)> m_onError{nullptr};

    std::unordered_map<int, std::unique_ptr<PacketQueue>> m_packetQueues{};
    std::thread m_workerThread{};
    std::atomic_bool m_exitFlag{false};

    int m_serial{0};
    std::mutex m_seekMutex{};
    std::condition_variable m_seekCV{};
    int64_t m_seekPositionUs{AV_NOPTS_VALUE};
};

} // namespace media