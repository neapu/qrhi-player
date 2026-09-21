#pragma once
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
#include "Packet.h"

namespace controller {
/**
 * @brief 包队列：解封装线程 → 解码线程 之间的有界缓冲。
 *
 * 本类型是被动数据结构，不归属任何线程：生产者(解封装线程)调push，消费者(解码线程)调pop，
 * 两者各自在自己的线程上访问，线程安全由内部mutex保证。
 *
 * 队列不含seek语义：过时的包该不该丢由调用方决策(push返回Interrupted后重判)，
 * 换代清空由调用方在seek成功点调clear()完成。
 */
class PacketQueue {
public:
    enum class PushResult {
        Pushed,      // 已入队
        Interrupted, // 队列满而等待时被打断(interrupt/clear/close)，包未入队，调用方需重新决策后重试
        Closed       // 队列已关闭，包被丢弃
    };

    struct Params {
        int streamIndex{-1};
        // 队列深度上限，按目标缓冲时长换算(见Controller::initialize)
        size_t maxDepth{100};
    };
    static std::unique_ptr<PacketQueue> create(const Params& params);

    int streamIndex() const { return m_streamIndex; }

    // 生产者(解封装线程)接口
    // 前置条件：packet非空(由调用方保证，解封装线程不会产出空包)
    PushResult push(PacketPtr&& packet);
    // 打断可能阻塞在push的生产者：其等待立即返回Interrupted，由调用方重新决策(如seek已投递则丢包)
    void interrupt();
    // 换代：清空在途旧包(seek成功时由解封装线程调用)，并唤醒可能阻塞的生产者
    void clear();

    // 消费者(解码线程)接口：队列空时阻塞；返回false表示队列已关闭，消费者应退出
    bool pop(PacketPtr& packet);

    // 停止：解除两端阻塞(生产者返回Closed，消费者返回false)，之后push一律丢弃。幂等
    void close();

private:
    PacketQueue() = default;
    bool initialize(const Params& params);

private:
    int m_streamIndex{-1};
    size_t m_maxDepth{100};

    // 生产者等待"水位下降/被打断"，消费者等待"有数据"，两者谓词不同，故用两个条件变量
    std::mutex m_mutex;
    std::condition_variable m_notFullCV;
    std::condition_variable m_notEmptyCV;
    std::deque<PacketPtr> m_queue;
    bool m_closed{false};
    // 打断标记：由生产者取走后重新决策，保证同一轮打断不被反复消费
    bool m_interrupted{false};
};

} // namespace controller
