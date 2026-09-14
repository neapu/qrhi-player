#include "ControllerImpl.h"
#include <print>
#include <chrono>
#include "SwsProcessor.h"
#include "SwrProcessor.h"

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
            videoDecodeParams.canDropFrames = true; // 视频可以丢帧

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
            // 要根据时长计算队列长度
            double sampleRate = stream->codecpar->sample_rate;
            sampleRate = sampleRate > 0 ? sampleRate : 44100; // 默认采样率为44100
            audioDecodeParams.maxPacketQueueDepth = static_cast<size_t>(TARGET_AUDIO_QUEUE_DURATION * sampleRate);
            audioDecodeParams.canDropFrames = false; // 音频一般不丢帧

            m_audioWorker = DecodeWorker::create(audioDecodeParams);
            if (!m_audioWorker) {
                LOGE("Failed to create Audio DecodeWorker");
                return false;
            }
        }
    }

    m_demuxerWorker->start();
    
    return true;
}

FramePtr Controller::nextVideoFrame() const
{
    return nullptr;
}

FramePtr Controller::nextAudioFrame() const
{
    return nullptr;
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
}

bool Controller::isPaused() const
{
    return false;
}

void Controller::onPacketRead(controller::PacketPtr&& packet)
{
    if (m_videoWorker && m_videoWorker->streamIndex() == packet->streamIndex()) {
        m_videoWorker->sendPacket(std::move(packet));
    } else if (m_audioWorker && m_audioWorker->streamIndex() == packet->streamIndex()) {
        m_audioWorker->sendPacket(std::move(packet));
    }

} // namespace controller