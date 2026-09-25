#include "PacketQueue.h"

namespace media {
PacketQueue::PacketQueue(size_t maxSize, bool dropOldest)
    : m_maxSize(maxSize), m_dropOldest(dropOldest)
{
}

void PacketQueue::push(fh::PacketPtr&& packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.size() >= m_maxSize) {
        if (m_dropOldest) {
            m_queue.pop_front();
        } else {
            m_nonFullCV.wait(lock, [this] { return m_queue.size() < m_maxSize || m_interrupted; });
            if (m_interrupted) {
                m_interrupted = false;
                return;
            }
        }
    }
    m_queue.push_back(std::move(packet));
    m_nonFullCV.notify_one();
}

fh::PacketPtr PacketQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
        return nullptr;
    }
    fh::PacketPtr packet = std::move(m_queue.front());
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
}

bool PacketQueue::empty() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

} // namespace media