#include "PacketQueue.h"

namespace media {
PacketQueue::PacketQueue(size_t capacity)
    : m_capacity(capacity)
{
}

bool PacketQueue::push(MediaPacket&& packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notFullCv.wait(lock, [this] { return m_queue.size() < m_capacity || m_interrupted || m_stopped; });
    if (m_stopped) {
        return false;
    }
    if (m_interrupted) {
        m_interrupted = false;
        // 如果被打断，也让包入队，超一点无所谓，如果seek成功会清空队列，如果失败，也不会丢包
    }
    m_queue.push_back(std::move(packet));
    m_notEmptyCv.notify_one();
    return true;
}

MediaPacket PacketQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notEmptyCv.wait(lock, [this] { return !m_queue.empty() || m_interrupted || m_stopped; });
    if (m_interrupted || m_stopped) {
        m_interrupted = false;
        return EmptyPacket{};
    }
    MediaPacket packet = std::move(m_queue.front());
    m_queue.pop_front();
    m_notFullCv.notify_one();
    return packet;
}

void PacketQueue::clear()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_interrupted = false;
    m_queue.clear();
    m_notFullCv.notify_all();
}

void PacketQueue::interrupt()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_interrupted = true;
    m_notEmptyCv.notify_all();
    m_notFullCv.notify_all();
}

void PacketQueue::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_stopped = true;
    m_notEmptyCv.notify_all();
    m_notFullCv.notify_all();
}

} // namespace media