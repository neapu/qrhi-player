#pragma once
#include <unordered_map>
#include <vector>
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
class DemuxWorkerBuilder;

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

    ~DemuxWorker();

    // 调用约定：启动工作线程，对象生命周期只能启动一次，停止后不能再启动
    // 重复调用会被忽略并打印错误日志
    // 返回初始序列号
    int start();
    // 调用约定：不能在回调中调用 stop；重复调用会被忽略并打印错误日志
    void stop();

    void seek(int64_t positionUs);

    MediaPacket popPacket(int streamIndex);
private:
    friend class DemuxWorkerBuilder;

    DemuxWorker(const Params& params, DemuxerPtr&& demuxer);
    bool initialize();
    bool configurePacketQueues(const std::vector<std::pair<int, size_t>>& packetQueueSpecs);

    // 实际的停止流程，可由 stop 和析构重复进入而不打印错误日志
    void stopInternal();

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
    std::atomic_bool m_started{false};
    std::atomic_bool m_stopped{false};

    int m_serial{0};
    std::mutex m_seekMutex{};
    std::condition_variable m_seekCV{};
    int64_t m_seekPositionUs{AV_NOPTS_VALUE};
};

// 用法：
//   auto worker = DemuxWorkerBuilder(params, std::move(demuxer))
//                     .withPacketQueue(videoIndex, 100)
//                     .withPacketQueue(audioIndex, 200)
//                     .build();
//   if (worker) { worker->start(); }
class DemuxWorkerBuilder {
public:
    DemuxWorkerBuilder(const DemuxWorker::Params& params, DemuxerPtr&& demuxer);

    // 声明需要消费的流：为该流创建包队列，缓存指定容量的媒体包。容量不允许为0。
    // 没有消费端的流不要创建队列，避免卡死解封装线程
    DemuxWorkerBuilder& withPacketQueue(int streamIndex, size_t capacity);

    // 返回已完成配置、可直接调用 start 的对象；配置失败（如流索引非法）返回 nullptr
    std::unique_ptr<DemuxWorker> build();
private:
    DemuxWorker::Params m_params{};
    DemuxerPtr m_demuxer{nullptr};
    std::vector<std::pair<int, size_t>> m_packetQueueSpecs{};
};

} // namespace media
