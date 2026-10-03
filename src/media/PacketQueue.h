#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include "MediaPacket.h"

namespace media {
class PacketQueue {
public:
    explicit PacketQueue(size_t capacity);

    bool push(MediaPacket&& packet);
    MediaPacket pop();
    void clear();
    void interrupt();
    void stop();

private:
    std::deque<MediaPacket> m_queue;
    size_t m_capacity;
    std::mutex m_mutex;
    std::condition_variable m_notEmptyCv;
    std::condition_variable m_notFullCv;
    bool m_interrupted{false};
    bool m_stopped{false};
};

} // namespace media