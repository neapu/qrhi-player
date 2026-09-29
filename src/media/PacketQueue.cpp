#include "PacketQueue.h"

namespace media {
PacketQueue::PacketQueue(size_t maxSize, bool dropOldest)
    : m_maxSize(maxSize), m_dropOldest(dropOldest)
{
}

void PacketQueue::push(MediaPacket&& packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.size() >= m_maxSize) {
        if (m_dropOldest) {
            m_queue.pop_front();
            ++m_droppedCount;
        } else {
            m_nonFullCV.wait(lock, [this] { return m_queue.size() < m_maxSize || m_interrupted || m_stopped; });
            if (m_interrupted) {
                m_interrupted = false;
                return;
            }
        }
    }
    if (m_stopped) {
        return;
    }
    m_queue.push_back(std::move(packet));
    m_nonEmptyCV.notify_one();
}

MediaPacket PacketQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
        m_nonEmptyCV.wait(lock, [this] { return !m_queue.empty() || m_interrupted || m_stopped; });
        if (m_interrupted) {
            m_interrupted = false;
            return EmptyPacket{};
        }
    }
    if (m_stopped) {
        return EmptyPacket{};
    }
    MediaPacket packet = std::move(m_queue.front());
    m_queue.pop_front();
    m_nonFullCV.notify_one();
    return packet;
}

void PacketQueue::clear()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_interrupted = false;
    m_queue.clear();
    m_nonFullCV.notify_all();
}

void PacketQueue::interrupt()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_interrupted = true;
    m_nonFullCV.notify_all();
    m_nonEmptyCV.notify_all();
}

bool PacketQueue::empty() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

void PacketQueue::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_stopped = true;
    m_nonFullCV.notify_all();
    m_nonEmptyCV.notify_all();
}

int64_t PacketQueue::droppedCount() const
{
    return m_droppedCount.load();
}

} // namespace media