#include "PacketQueue.h"

namespace controller {
std::unique_ptr<PacketQueue> PacketQueue::create(const Params& params)
{
    auto queue = std::unique_ptr<PacketQueue>(new PacketQueue());
    if (!queue->initialize(params)) {
        return nullptr;
    }
    return queue;
}

bool PacketQueue::initialize(const Params& params)
{
    if (params.maxDepth == 0) {
        return false;
    }
    m_streamIndex = params.streamIndex;
    m_maxDepth = params.maxDepth;
    return true;
}

PacketQueue::PushResult PacketQueue::push(PacketPtr&& packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed) {
        return PushResult::Closed;
    }
    if (m_interrupted) {
        // 上一轮打断尚未交给调用方决策：先让调用方重新判断过时性，避免过时数据入队
        m_interrupted = false;
        return PushResult::Interrupted;
    }
    if (m_queue.size() >= m_maxDepth) {
        // 队列满时阻塞生产者(背压)。这里不丢包：队列压力无法区分"渲染落后"和"解码暂时慢"，
        // 在这里丢会误丢不迟到的帧。视频消费速度不足的压力由帧队列出队侧按主时钟丢帧消化
        m_notFullCV.wait(lock, [this]() {
            return m_queue.size() < m_maxDepth || m_closed || m_interrupted;
        });
        if (m_closed) {
            return PushResult::Closed;
        }
        if (m_interrupted) {
            m_interrupted = false;
            return PushResult::Interrupted;
        }
    }
    m_queue.push_back(std::move(packet));
    m_notEmptyCV.notify_one();
    return PushResult::Pushed;
}

void PacketQueue::interrupt()
{
    {
        // 持锁后再notify：调用方的等待谓词在同一把锁内求值，
        // 保证不会出现"谓词已求值、尚未进入wait"时丢掉本次唤醒
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interrupted = true;
    }
    m_notFullCV.notify_all();
}

void PacketQueue::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }
    // 清空后水位必然下降，唤醒可能阻塞的生产者
    m_notFullCV.notify_all();
}

bool PacketQueue::pop(PacketPtr& packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed) {
        return false;
    }
    m_notEmptyCV.wait(lock, [this]() {
        return !m_queue.empty() || m_closed;
    });
    if (m_closed) {
        return false;
    }
    packet = std::move(m_queue.front());
    m_queue.pop_front();
    m_notFullCV.notify_one();
    return true;
}

void PacketQueue::close()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_notFullCV.notify_all();
    m_notEmptyCV.notify_all();
}

} // namespace controller
