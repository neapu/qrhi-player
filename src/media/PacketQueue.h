#pragma once
#include "Packet.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include "MediaPacket.h"

namespace media {
class PacketQueue {
public:
    PacketQueue(size_t maxSize, bool dropOldest = false);
    void push(MediaPacket&& packet);
    MediaPacket pop();
    void clear();
    void interrupt();
    bool empty() const;

private:
    std::deque<MediaPacket> m_queue{};
    mutable std::mutex m_mutex{};
    std::condition_variable m_nonFullCV{};
    std::condition_variable m_nonEmptyCV{};
    size_t m_maxSize{0};
    bool m_dropOldest{false};
    bool m_interrupted{false};
};
using PacketQueuePtr = std::unique_ptr<PacketQueue>;
} // namespace media