#include "DemuxerWorker.h"

namespace controller {
std::unique_ptr<DemuxerWorker> DemuxerWorker::create(const Params& params)
{
    auto worker = std::unique_ptr<DemuxerWorker>(new DemuxerWorker());
    if (!worker->initialize(params)) {
        return nullptr;
    }
    return worker;
}

DemuxerWorker::~DemuxerWorker()
{
    stop();
}

void DemuxerWorker::start()
{
    m_exitFlag = false;
    m_thread = std::thread(&DemuxerWorker::workerFunc, this);
}

void DemuxerWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        if (m_exitFlag.exchange(true)) {
            return; // 已停止，锁随作用域释放
        }
    }
    // 唤醒可能在EOF等待中的线程
    m_seekCV.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

double DemuxerWorker::duration() const
{
    return m_demuxer ? m_demuxer->duration() : 0.0;
}

void DemuxerWorker::seek(int streamIndex, int64_t pts)
{
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_seekRequest = SeekRequest{streamIndex, pts};
    }
    m_seekCV.notify_all();
}

bool DemuxerWorker::initialize(const Params& params)
{
    if (!params.logger || !params.onPacketRead) {
        return false;
    }
    m_logger = params.logger;
    auto tracer = m_logger->trace();

    m_onPacketRead = params.onPacketRead;
    m_demuxer = Demuxer::create(params.url, params.logger);
    if (!m_demuxer) {
        LOGE("Failed to create Demuxer");
        return false;
    }
    
    return true;
}

void DemuxerWorker::workerFunc()
{
    auto tracer = m_logger->trace();
    // EOF后不退出线程：置eof状态并阻塞等待seek恢复或stop退出，
    // 这样播完后用户仍可拖动进度条回看
    bool eof = false;
    while (!m_exitFlag) {
        // 消费seek请求
        SeekRequest request{};
        {
            std::lock_guard<std::mutex> lock(m_seekMutex);
            request = m_seekRequest;
            m_seekRequest = {};
        }
        if (request.pts != AV_NOPTS_VALUE) {
            if (m_demuxer->seek(request.streamIndex, request.pts)) {
                m_serial.fetch_add(1);
                eof = false; // 从EOF恢复读取
            } else {
                // seek失败保持eof状态，位置未变，继续等待下一个请求
                LOGE("Failed to seek");
            }
        }

        if (eof) {
            // 已到文件尾且无seek请求：阻塞等待seek或退出
            std::unique_lock<std::mutex> lock(m_seekMutex);
            m_seekCV.wait(lock, [this]() {
                return m_seekRequest.pts != AV_NOPTS_VALUE || m_exitFlag.load();
            });
            // 回到循环顶部消费请求或退出
            continue;
        }

        auto packet = m_demuxer->readPacket(m_serial.load());
        if (!packet) {
            LOGE("Failed to read packet");
            // 暂时只考虑本地文件，调试阶段先直接中止程序
            std::abort();
        }

        if (packet->type() == Packet::PacketType::End) {
            LOGI("End of stream reached");
            // 只记录状态不退出线程，End包照常投递
            eof = true;
        }
        m_onPacketRead(std::move(packet));
    }

}
} // namespace controller