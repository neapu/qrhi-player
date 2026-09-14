#include "ControllerImpl.h"
#include <algorithm>
#include <cstdlib>
#include <print>
#include <chrono>
#include "SwsProcessor.h"
#include "SwrProcessor.h"

namespace {
int64_t steadyTimeUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 音画同步校准参数。音频渲染侧上报的位置按设备buffer粒度量化且回调时机带抖动，
// 时钟不能被每次回调直接重置，否则视频帧放行节奏随上报抖动来回跳：
// - 偏差在死区内：不校正，过滤上报抖动
// - 中等偏差：时钟以95%/105%速率平滑追赶(约50ms/s)，期间时钟保持单调连续无跳变，
//   视频帧时长变化±5%不可感知
// - 偏差过大：追赶需数秒以上，直接对齐一次
constexpr int64_t SYNC_DEAD_BAND_US = 25 * 1000; // 死区约一帧以内
constexpr int64_t SYNC_SNAP_US = 300 * 1000;
constexpr double SYNC_CATCH_UP_SPEED = 1.05;
constexpr double SYNC_FALL_BACK_SPEED = 0.95;

}

namespace controller {
std::unique_ptr<IController> IController::create(const Params& params)
{
    auto controller = std::make_unique<Controller>(params);
    if (controller->initialize()) {
        return controller;
    }
    return nullptr;
}

Controller::Controller(const Params& params) : m_params(params)
{
}

Controller::~Controller()
{
    FUNC_TRACE();
    // 需要先解除解码线程入队阻塞，才能停止解封装线程
    if (m_videoWorker) {
        m_videoWorker->stop();
    }
    if (m_audioWorker) {
        m_audioWorker->stop();
    }
    if (m_demuxerWorker) {
        m_demuxerWorker->stop();
    }
}

bool Controller::initialize()
{
    if (m_params.logCallback) {
        m_logger = std::make_shared<Logger>(m_params.logCallback);
    } else {
        m_logger = std::make_shared<Logger>([](LogLevel level, const std::string& fileName, int line, const std::string& message) {
            std::string logLevel{"Debug"};
            switch (level) {
                case LogLevel::Debug:
                    logLevel = "Debug";
                    break;
                case LogLevel::Info:
                    logLevel = "Info";
                    break;
                case LogLevel::Warning:
                    logLevel = "Warning";
                    break;
                case LogLevel::Error:
                    logLevel = "Error";
                    break;
                case LogLevel::Fatal:
                    logLevel = "Fatal";
                    break;
            };
            const auto now = std::chrono::system_clock::now();
            const auto timet = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &timet); // std::localtime 返回静态缓冲区，多线程调用是数据竞争
#else
            localtime_r(&timet, &tm);
#endif
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            std::print("[{}-{:02}-{:02} {:02}:{:02}:{:02}.{}] [{}] {}:{} {}\n",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec,
                       static_cast<int>(ms),
                       logLevel, fileName, line, message);
        });
    }
    
    auto tracer = m_logger->trace();

    LOGI("Opening file: " << m_params.url);

    constexpr size_t TARGET_VIDEO_QUEUE_DURATION = 10; // 视频缓冲时长(包队列)
    constexpr size_t TARGET_AUDIO_QUEUE_DURATION = 3; // 音频比视频短

    DemuxerWorker::Params demuxerParams;
    demuxerParams.url = m_params.url;
    demuxerParams.logger = m_logger;
    demuxerParams.onPacketRead = [this](controller::PacketPtr&& packet) {
        onPacketRead(std::move(packet));
    };
    m_demuxerWorker = DemuxerWorker::create(demuxerParams);
    if (!m_demuxerWorker) {
        LOGE("Failed to create DemuxerWorker");
        return false;
    }

    auto& demuxer = m_demuxerWorker->demuxer();
    for (int i = 0; i < demuxer->streamCount(); ++i) {
        auto* stream = demuxer->stream(i);
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            SwsProcessor::TargetFormat targetFormat{};
            targetFormat.format = AV_PIX_FMT_YUV420P;
            std::shared_ptr<SwsProcessor> swsProcessor = std::make_shared<SwsProcessor>(targetFormat);

            DecodeWorker::Params videoDecodeParams{};
            videoDecodeParams.stream = stream;
            videoDecodeParams.logger = m_logger;
            videoDecodeParams.frameProcessors.push_back(swsProcessor);
            // 要根据时长计算队列长度
            double fps = av_q2d(stream->avg_frame_rate);
            fps = fps > 0 ? fps : av_q2d(stream->r_frame_rate);
            fps = fps > 0 ? fps : 30.0; // 默认帧率为30
            videoDecodeParams.maxPacketQueueDepth = static_cast<size_t>(TARGET_VIDEO_QUEUE_DURATION * fps);
            // 视频允许丢帧：出队侧按主时钟丢弃被覆盖的到期帧
            videoDecodeParams.canDropFrames = true;

            m_videoWorker = DecodeWorker::create(videoDecodeParams);
            if (!m_videoWorker) {
                LOGE("Failed to create Video DecodeWorker");
                return false;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            SwrProcessor::TargetFormat targetFormat{};
            targetFormat.format = AV_SAMPLE_FMT_S16;
            std::shared_ptr<SwrProcessor> swrProcessor = std::make_shared<SwrProcessor>(targetFormat);

            DecodeWorker::Params audioDecodeParams{};
            audioDecodeParams.stream = stream;
            audioDecodeParams.logger = m_logger;
            audioDecodeParams.frameProcessors.push_back(swrProcessor);
            // 要根据时长计算队列长度：包速率 = 采样率 / 每包样本数(frame_size)，
            // 部分编码frame_size未知时按1024(AAC典型值)估算
            double sampleRate = stream->codecpar->sample_rate;
            sampleRate = sampleRate > 0 ? sampleRate : 44100; // 默认采样率为44100
            double frameSize = stream->codecpar->frame_size;
            frameSize = frameSize > 0 ? frameSize : 1024.0;
            audioDecodeParams.maxPacketQueueDepth = std::max<size_t>(
                static_cast<size_t>(TARGET_AUDIO_QUEUE_DURATION * sampleRate / frameSize), 16);
            audioDecodeParams.canDropFrames = false; // 音频一般不丢帧
            audioDecodeParams.controlClock = false; // 音频不做严格时钟门控，按阈值放行

            m_audioWorker = DecodeWorker::create(audioDecodeParams);
            if (!m_audioWorker) {
                LOGE("Failed to create Audio DecodeWorker");
                return false;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_clock.mutex);
        reanchorLocked(0, 1.0);
        m_clock.paused = false;
    }
    m_demuxerWorker->start();
    
    return true;
}

FramePtr Controller::nextVideoFrame() const
{
    if (!m_videoWorker) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        return nullptr;
    }

    return m_videoWorker->receiveFrame(clockUsLocked());
}

FramePtr Controller::nextAudioFrame() const
{
    if (!m_audioWorker) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        return nullptr;
    }

    return m_audioWorker->receiveFrame(clockUsLocked());
}

void Controller::audioRenderTime(int64_t renderTimeUs)
{
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        m_clock.pausedMediaUs = renderTimeUs;
        return;
    }

    const int64_t driftUs = renderTimeUs - clockUsLocked();

    if (std::abs(driftUs) <= SYNC_DEAD_BAND_US) {
        // 死区内不校正；若此前正在追赶，回到正常速率并停止累计
        reanchorLocked(clockUsLocked(), 1.0);
        return;
    }
    if (std::abs(driftUs) >= SYNC_SNAP_US) {
        // 偏差过大(起播/seek/失步)，追赶耗时超过跳变本身的代价，直接对齐
        reanchorLocked(renderTimeUs, 1.0);
        return;
    }
    // 中等偏差：以微调速率平滑收敛，时钟单调连续，不产生跳变
    reanchorLocked(clockUsLocked(), driftUs > 0 ? SYNC_CATCH_UP_SPEED : SYNC_FALL_BACK_SPEED);
}

double Controller::duration() const
{
    return m_demuxerWorker ? m_demuxerWorker->duration() : 0.0;
}

void Controller::seek(double timepoint)
{
}

void Controller::pauseOrResume()
{
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        m_clock.paused = false;
        reanchorLocked(m_clock.pausedMediaUs, 1.0);
    } else {
        m_clock.pausedMediaUs = clockUsLocked();
        m_clock.paused = true;
    }
}

bool Controller::isPaused() const
{
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    return m_clock.paused;
}

void Controller::onPacketRead(controller::PacketPtr&& packet)
{
    if (m_videoWorker && m_videoWorker->streamIndex() == packet->streamIndex()) {
        m_videoWorker->sendPacket(std::move(packet));
    } else if (m_audioWorker && m_audioWorker->streamIndex() == packet->streamIndex()) {
        m_audioWorker->sendPacket(std::move(packet));
    }
}

int64_t Controller::clockUsLocked() const
{
    if (m_clock.paused) {
        return m_clock.pausedMediaUs;
    }
    const auto elapsedUs = static_cast<int64_t>(
        static_cast<double>(steadyTimeUs() - m_clock.anchorWallUs) * m_clock.speed);
    return m_clock.anchorMediaUs + elapsedUs;
}

void Controller::reanchorLocked(int64_t mediaUs, double speed)
{
    m_clock.anchorWallUs = steadyTimeUs();
    m_clock.anchorMediaUs = mediaUs;
    m_clock.speed = speed;
}

} // namespace controller