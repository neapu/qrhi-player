#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include "MediaPacket.h"

namespace media {
class PacketQueue {
public:
    explicit PacketQueue(size_t capacity);

    // 非阻塞模式时，强制入队，不管容量是否已满，用于EndPacket等特殊包
    bool push(MediaPacket&& packet, bool nonBlocking);
    MediaPacket pop();
    void clear();
    void interrupt();
    void cancelInterrupt();
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