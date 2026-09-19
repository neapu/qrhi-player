#include "ControllerImpl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <print>
#include <chrono>
#include "SwsProcessor.h"
#include "SwrProcessor.h"
#include "HWTransferProcessor.h"
#include "ffmpeg_helper/FFmpegError.h"

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

    auto demuxer = Demuxer::create(m_params.url, m_logger);

    for (int i = 0; i < demuxer->streamCount(); ++i) {
        auto* stream = demuxer->stream(i);
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !m_videoWorker) {
            auto videoDecoder = createVideoDecoder(stream, demuxer);
            if (!videoDecoder) {
                LOGE("Failed to create Video Decoder");
                return false;
            }

            DecodeWorker::Params videoDecodeParams{};
            videoDecodeParams.stream = stream;
            videoDecodeParams.logger = m_logger;

            if (videoDecoder->type() == Decoder::Type::Software) {
                SwsProcessor::TargetFormat targetFormat{};
                targetFormat.format = AV_PIX_FMT_YUV420P;
                std::shared_ptr<SwsProcessor> swsProcessor = std::make_shared<SwsProcessor>(targetFormat, m_logger);
                videoDecodeParams.frameProcessors.push_back(swsProcessor);
            } else if (m_params.enableHwTransfer) {
                std::shared_ptr<HWTransferProcessor> hwTransferProcessor = std::make_shared<HWTransferProcessor>(m_logger);
                videoDecodeParams.frameProcessors.push_back(hwTransferProcessor);
                SwsProcessor::TargetFormat targetFormat{};
                targetFormat.format = AV_PIX_FMT_YUV420P;
                std::shared_ptr<SwsProcessor> swsProcessor = std::make_shared<SwsProcessor>(targetFormat, m_logger);
                videoDecodeParams.frameProcessors.push_back(swsProcessor);
            } else {
                // 硬件解码并且不转换硬件帧，不需要后处理
                LOGI("Hardware decoding without hardware frame transfer, no post-processing needed.");
            }

            // 要根据时长计算队列长度
            double fps = av_q2d(stream->avg_frame_rate);
            fps = fps > 0 ? fps : av_q2d(stream->r_frame_rate);
            fps = fps > 0 ? fps : 30.0; // 默认帧率为30
            videoDecodeParams.maxPacketQueueDepth = static_cast<size_t>(TARGET_VIDEO_QUEUE_DURATION * fps);
            // 视频允许丢帧：出队侧按主时钟丢弃被覆盖的到期帧
            videoDecodeParams.canDropFrames = true;
            // pending seek时丢弃在途旧包并打断包队列满的阻塞，保证解封装线程及时消费seek请求
            videoDecodeParams.interrupt = [this]() {
                return m_demuxerWorker && m_demuxerWorker->seekPending();
            };
            
            m_videoWorker = DecodeWorker::create(videoDecodeParams, std::move(videoDecoder));
            if (!m_videoWorker) {
                LOGE("Failed to create Video DecodeWorker");
                return false;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !m_audioWorker) {
            SwrProcessor::TargetFormat targetFormat{};
            targetFormat.format = AV_SAMPLE_FMT_S16;
            std::shared_ptr<SwrProcessor> swrProcessor = std::make_shared<SwrProcessor>(targetFormat, m_logger);

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
            audioDecodeParams.interrupt = [this]() {
                return m_demuxerWorker && m_demuxerWorker->seekPending();
            };

            Decoder::Params decoderParams{};
            decoderParams.type = Decoder::Type::Software;
            decoderParams.stream = stream;
            decoderParams.logger = m_logger;
            auto decoder = Decoder::create(decoderParams);
            m_audioWorker = DecodeWorker::create(audioDecodeParams, std::move(decoder));
            if (!m_audioWorker) {
                LOGE("Failed to create Audio DecodeWorker");
                return false;
            }
            // 从已打开的解码上下文取输出参数(比codecpar更准，解码器open时可能修正)；
            // SwrProcessor对采样率/声道是透传，帧参数即解码器参数，采样格式固定为S16
            if (const auto* codecCtx = m_audioWorker->codecContext()) {
                AudioParams params;
                params.sampleRate = codecCtx->sample_rate;
                params.channels = codecCtx->ch_layout.nb_channels;
                params.sampleFormat = IFrame::SampleFormat::S16LE;
                m_audioParams = params;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_clock.mutex);
        reanchorLocked(0, 1.0);
        m_clock.paused = false;
    }
    
    DemuxerWorker::Params demuxerParams;
    demuxerParams.logger = m_logger;
    demuxerParams.onPacketRead = [this](controller::PacketPtr&& packet) {
        onPacketRead(std::move(packet));
    };
    demuxerParams.onSeekSucceeded = [this](int serial, int64_t targetUs) {
        onSeekSucceeded(serial, targetUs);
    };
    m_demuxerWorker = DemuxerWorker::create(demuxerParams, std::move(demuxer));
    if (!m_demuxerWorker) {
        LOGE("Failed to create DemuxerWorker");
        return false;
    }
    
    return true;
}

FramePtr Controller::nextVideoFrame()
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

FramePtr Controller::nextAudioFrame()
{
    if (!m_audioWorker) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        return nullptr;
    }

    auto frame = m_audioWorker->receiveFrame(clockUsLocked());
    if (frame && frame->serial() != m_clock.servedAudioSerial) {
        m_clock.servedAudioSerial = frame->serial();
        if (frame->type() == IFrame::FrameType::Normal && frame->pts() != AV_NOPTS_VALUE) {
            // seek后首个新段音频帧：按实际落点重锚时钟，
            // 吸收AVSEEK_FLAG_BACKWARD落在关键帧导致的回退偏差。
            // 注意：音频回校抑制路径需音频渲染侧调用audioRenderTime才能验证，
            // 当前view层尚无音频回调，此逻辑留待音频侧接入后联调
            reanchorLocked(frame->pts(), 1.0);
        }
    }
    return frame;
}

void Controller::audioRenderTime(int64_t renderTimeUs)
{
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.suppressAudioSync) {
        // seek过渡期：音频设备缓冲仍在播旧段，其上报会把时钟snap回旧位置；
        // 忽略直到上报位置与时钟偏差落入正常校准范围(新段已实际发声)
        if (std::abs(renderTimeUs - clockUsLocked()) >= SYNC_SNAP_US) {
            return;
        }
        m_clock.suppressAudioSync = false;
    }

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

std::optional<IController::AudioParams> Controller::audioParams() const
{
    return m_audioParams;
}

double Controller::duration() const
{
    return m_demuxerWorker ? m_demuxerWorker->duration() : 0.0;
}

double Controller::position() const
{
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    return static_cast<double>(clockUsLocked()) / 1'000'000.0;
}

void Controller::seek(double timepoint)
{
    if (!m_demuxerWorker) {
        return;
    }
    // clamp到合法区间；clamp到duration处会很快读到EOF，属预期行为
    const double dur = duration();
    if (timepoint < 0) {
        timepoint = 0;
    } else if (dur > 0 && timepoint > dur) {
        timepoint = dur;
    }
    const int64_t targetUs = static_cast<int64_t>(std::llround(timepoint * AV_TIME_BASE));

    LOGI("Seeking to " << timepoint << "s");

    // 仅投递请求，av_seek_frame与时钟重锚都在解封装线程成功后触发(onSeekSucceeded)，
    // 不阻塞UI线程；seek失败时serial不递增、回调不触发，时钟与播放无缝保持原状
    m_demuxerWorker->seek(-1, targetUs); // -1=容器默认流，pts为AV_TIME_BASE单位
    // 唤醒可能因包队列满阻塞在sendPacket的解封装线程，使其尽快回到循环顶部消费seek请求
    if (m_videoWorker) {
        m_videoWorker->interrupt();
    }
    if (m_audioWorker) {
        m_audioWorker->interrupt();
    }
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

StatisticsData Controller::statistics() const
{
    StatisticsData stats{};
    if (m_videoWorker) {
        stats.video.droppedFrames = m_videoWorker->droppedFrames();
        stats.video.decodedFrames = m_videoWorker->decodedFrames();
    }
    return stats;
}

void Controller::onPacketRead(controller::PacketPtr&& packet)
{
    if (packet->type() == controller::Packet::PacketType::End) {
        // 流结束，音视频线程都要通知到
        if (m_videoWorker) {
            auto endPacket = Packet::create(packet->serial(), controller::Packet::PacketType::End);
            m_videoWorker->sendPacket(std::move(endPacket));
        }
        if (m_audioWorker) {
            auto endPacket = Packet::create(packet->serial(), controller::Packet::PacketType::End);
            m_audioWorker->sendPacket(std::move(endPacket));
        }
    }
    if (m_videoWorker && m_videoWorker->streamIndex() == packet->streamIndex()) {
        m_videoWorker->sendPacket(std::move(packet));
    } else if (m_audioWorker && m_audioWorker->streamIndex() == packet->streamIndex()) {
        m_audioWorker->sendPacket(std::move(packet));
    }
}

void Controller::onSeekSucceeded(int serial, int64_t targetUs)
{
    // 解封装线程上回调，av_seek_frame已成功、serial已递增
    std::lock_guard<std::mutex> lock(m_clock.mutex);
    m_clock.suppressAudioSync = (m_audioWorker != nullptr);
    if (m_clock.paused) {
        m_clock.pausedMediaUs = targetUs;
    } else {
        reanchorLocked(targetUs, 1.0);
    }
    LOGI("Seek succeeded, serial=" << serial << " targetUs=" << targetUs);
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

DecoderPtr Controller::createVideoDecoder(const AVStream* stream, DemuxerPtr& demuxer)
{
    Decoder::Params decoderParams{};
    decoderParams.stream = stream;
    decoderParams.logger = m_logger;
    decoderParams.type = Decoder::Type::Software;
#ifdef _WIN32
    if (m_params.enableHwDecoder) {
        decoderParams.type = Decoder::Type::Dxva;
        decoderParams.d3d11Device = m_params.d3d11Device;
    }
#endif

    if (decoderParams.type == Decoder::Type::Software) {
        return Decoder::create(decoderParams);
    }
    
    // 硬件解码器创建失败时需要回退到软件解码
    auto hwDecoder = Decoder::create(decoderParams);
    if (!hwDecoder) {
        decoderParams.type = Decoder::Type::Software;
        return Decoder::create(decoderParams);
    }

    // 解码一帧测试，如果失败了，需要回退到软件解码
    auto ret = testHardwareDecoder(hwDecoder, demuxer, stream->index);
    if (!demuxer->seek(-1, 0)) { // 回退到流的起始位置
        LOGW("Failed to seek demuxer to the beginning after hardware decoder test");
    }
    if (!ret) {
        decoderParams.type = Decoder::Type::Software;
        return Decoder::create(decoderParams);
    }
    hwDecoder->flush();

    return hwDecoder;
}

bool Controller::testHardwareDecoder(DecoderPtr& decoder, DemuxerPtr& demuxer, int streamIndex)
{
    for (;;) {
        auto packet = demuxer->readPacket(0);
        if (!packet) {
            LOGW("Failed to read packet for hardware decoder test");
            return false;
        }
        if (packet->type() == Packet::PacketType::End) {
            LOGW("Reached end of stream during hardware decoder test");
            return false;
        }
        if (packet->streamIndex() != streamIndex) {
            continue;
        }
        bool ret = decoder->sendPacket(std::move(packet));
        if (!ret) {
            LOGW("Hardware decoder test failed");
            return false;
        }
        for (;;) {
            auto frameExp = decoder->receiveFrame(0);
            if (!frameExp) {
                int err = frameExp.error();
                // 如果是EAGAIN或者EOF，继续尝试接收下一帧
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOGW("Hardware decoder test failed with error: " << fh::err2str(err));
                return false;
            }
            // 成功接收到一帧，说明硬件解码器工作正常
            return true;
        }
    }
    return false;
}

} // namespace controller