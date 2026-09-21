#include "FrameQueue.h"

namespace controller {
namespace {
// 时钟策略为false时，允许放行的阈值：只有跳变超过阈值(如seek)才会被时钟控制
constexpr int64_t CONTROL_CLOCK_THRESHOLD_US = 200000; // 200ms
} // namespace

std::unique_ptr<FrameQueue> FrameQueue::create(const Params& params)
{
    auto queue = std::unique_ptr<FrameQueue>(new FrameQueue());
    if (!queue->initialize(params)) {
        return nullptr;
    }
    return queue;
}

bool FrameQueue::initialize(const Params& params)
{
    if (params.maxDepth == 0) {
        return false;
    }
    m_maxDepth = params.maxDepth;
    m_canDropFrames = params.canDropFrames;
    m_controlClock = params.controlClock;
    return true;
}

FrameQueue::PushResult FrameQueue::push(std::unique_ptr<Frame>&& frame, bool waitForRoom)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed) {
        return PushResult::Closed;
    }
    const int serial = frame->serial();
    if (serial != m_generation) {
        // seek已换代：手上这帧属于旧位置，注定不会被显示，直接丢弃
        return PushResult::StaleRejected;
    }
    if (waitForRoom && m_queue.size() >= m_maxDepth) {
        // 队列满时阻塞生产者(背压)，等待消费侧取走帧；
        // 换代/关闭也要打断等待，否则手里的旧帧会白白占着线程
        m_notFullCV.wait(lock, [this, serial]() {
            return m_queue.size() < m_maxDepth || m_closed || serial != m_generation;
        });
        if (m_closed) {
            return PushResult::Closed;
        }
        if (serial != m_generation) {
            return PushResult::StaleRejected;
        }
    }
    m_queue.push_back(std::move(frame));
    return PushResult::Pushed;
}

size_t FrameQueue::reset(int serial)
{
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_generation = serial;
        dropped = m_queue.size();
        m_queue.clear();
    }
    // 唤醒可能因帧队列满而阻塞在push的解码线程：代次已变，其等待谓词判定手中旧帧作废并丢弃
    m_notFullCV.notify_all();
    return dropped;
}

void FrameQueue::close()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_notFullCV.notify_all();
}

FramePtr FrameQueue::pop(int64_t playTimeUs)
{
    // 取帧不阻塞，如果队列为空则返回nullptr
    std::lock_guard<std::mutex> lock(m_mutex);

    // 丢帧策略：出队侧按主时钟丢弃。
    // 若队列中的下一帧也已到期(pts <= playTimeUs)，说明队头帧已被更新的帧覆盖，
    // 必然不会被显示，直接丢弃。每次调用丢弃0~N帧，数量由实际迟到程度决定，
    // 与帧率/渲染速度的比例无关；同时保证返回的是"已到期帧中最新的"一帧
    if (m_canDropFrames) {
        size_t dropCount = 0;
        while (m_queue.size() >= 2) {
            const auto& next = m_queue[1];
            // End等标记帧不参与丢帧判断，避免误丢结尾最后一帧正常帧
            if (next->type() != IFrame::FrameType::Normal || next->pts() > playTimeUs) {
                break;
            }
            m_queue.pop_front();
            ++dropCount;
        }
        if (dropCount > 0) {
            m_droppedFrames.fetch_add(dropCount, std::memory_order_relaxed);
            m_notFullCV.notify_one();
        }
    }

    if (m_queue.empty()) {
        return nullptr;
    }
    // 先观察队头帧的pts是否可以放行
    int64_t pts = m_queue.front()->pts();
    // 根据时钟策略判断是否放行
    // 默认postProcessFrame会将pts单位转换为微秒(us)
    if (m_controlClock) { // 视频模式
        if (pts > playTimeUs) {
            // 帧还没到播放时间，返回nullptr
            return nullptr;
        }
    } else { // 音频模式
        // 只有跳变超过阈值才会被时钟控制
        if (pts > playTimeUs + CONTROL_CLOCK_THRESHOLD_US) {
            // 音频帧跳变超过阈值，返回nullptr
            return nullptr;
        }
    }

    // 从帧队列中取出帧
    std::unique_ptr<Frame> frame = std::move(m_queue.front());
    m_queue.pop_front();
    m_notFullCV.notify_one();

    return frame;
}

} // namespace controller
