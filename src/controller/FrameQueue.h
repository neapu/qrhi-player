#pragma once
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "FrameImpl.h"

namespace controller {
/**
 * @brief 帧队列：解码线程 → 消费线程(UI线程/音频设备回调线程) 之间的有界缓冲。
 *
 * 本类型是被动数据结构，不归属任何线程：生产者(解码线程)调push，消费者(UI/音频线程)调pop。
 * 消费侧pop不阻塞(未到播放时间直接返回nullptr)，因此只需要一个条件变量供生产者在队列满时等待。
 *
 * 队列持有"当前代次"：seek换代后，解码线程手里在途的旧代次帧会被push拒收，
 * 保证消费侧不会拿到属于旧位置的数据。换代由Controller在seek成功点调reset()完成。
 */
class FrameQueue {
public:
    enum class PushResult {
        Pushed,        // 已入队
        StaleRejected, // 帧属旧代次(seek已换代)，被丢弃
        Closed         // 队列已关闭，帧被丢弃，生产者应退出
    };

    struct Params {
        // 队列深度上限
        size_t maxDepth{5};
        // 允许丢帧(视频)：出队侧按主时钟丢弃被新帧覆盖的到期帧
        bool canDropFrames{false};
        // 时钟策略：true=严格按时钟放行(视频)；false=时钟跳变超过阈值才限制(音频)
        bool controlClock{true};
    };
    static std::unique_ptr<FrameQueue> create(const Params& params);

    // 生产者(解码线程)接口
    // 前置条件：frame非空(由调用方保证)
    // waitForRoom=false用于End等标记帧：不受水位限制直接入队，避免"结尾"信号被消费侧暂停卡住
    PushResult push(std::unique_ptr<Frame>&& frame, bool waitForRoom = true);

    // 换代(seek成功时由解封装线程调用)：作废在途旧帧并设置新代次
    // @return 被丢弃的帧数，供调用方记录诊断日志
    size_t reset(int serial);

    // 停止：之后push一律返回Closed并解除其等待；已在队列中的帧仍可取走。幂等
    void close();

    // 消费者(UI/音频线程)接口：非阻塞，没有可放行的帧时返回nullptr
    FramePtr pop(int64_t playTimeUs);

    // 消费侧累计丢帧数(按主时钟丢弃)，用于统计/调试
    uint64_t droppedFrames() const { return m_droppedFrames.load(std::memory_order_relaxed); }

private:
    FrameQueue() = default;
    bool initialize(const Params& params);

private:
    size_t m_maxDepth{5};
    bool m_canDropFrames{false};
    bool m_controlClock{true};

    std::mutex m_mutex;
    std::condition_variable m_notFullCV;
    // 这里不用FramePtr，因为FramePtr声明的是IFrame抽象类，内部处理时需要具体的Frame实现
    std::deque<std::unique_ptr<Frame>> m_queue;
    bool m_closed{false};
    // 当前代次，与包队列中包的serial对应(换代见reset)
    int m_generation{0};
    // 消费端累计丢帧数，用于统计/调试
    std::atomic_uint64_t m_droppedFrames{0};
};

} // namespace controller
