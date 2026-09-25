#pragma once
#include "Packet.h"
#include <queue>
#include <mutex>
#include <condition_variable>

namespace media {
class PacketQueue {
public:
    PacketQueue(size_t maxSize, bool dropOldest = false);
    void push(fh::PacketPtr&& packet);
    // 非阻塞，若队列为空则立即返回nullptr
    fh::PacketPtr pop();
    void clear();
    void interrupt();
    bool empty() const;

private:
    std::deque<fh::PacketPtr> m_queue{};
    mutable std::mutex m_mutex{};
    std::condition_variable m_nonFullCV{};
    size_t m_maxSize{0};
    bool m_dropOldest{false};
    bool m_interrupted{false};
};
using PacketQueuePtr = std::unique_ptr<PacketQueue>;
} // namespace media