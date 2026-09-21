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
constexpr size_t TARGET_VIDEO_QUEUE_DURATION = 10; // 视频缓冲时长(包队列)
constexpr size_t TARGET_AUDIO_QUEUE_DURATION = 3; // 音频比视频短

int64_t steadyTimeUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool containsPixelFormat(const std::vector<controller::Frame::PixelFormat>& pixelFormats, controller::Frame::PixelFormat target)
{
    return std::find(pixelFormats.begin(), pixelFormats.end(), target) != pixelFormats.end();
}

AVPixelFormat getPreferredSupportedPixelFormat(const std::vector<controller::Frame::PixelFormat>& pixelFormats)
{
    for (const auto& format : pixelFormats) {
        if (format < controller::Frame::PixelFormat::D3D11) {
            return controller::Frame::toAvPixelFormat(format);
        }
    }
    return AV_PIX_FMT_NONE;
}

AVPixelFormat dxvaDecoderSoftwareFramePixelFormat(int bit)
{
    switch (bit) {
        case 8:
            return AV_PIX_FMT_NV12;
        case 10:
            return AV_PIX_FMT_P010LE;
        default:
            return AV_PIX_FMT_NONE;
    }
}
} // namespace

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
    // 先停解码线程：stop()会关闭两个队列，同时解除解封装线程在包队列满上的阻塞，
    // 之后才能停止解封装线程(否则它可能卡在push里)
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
    initializeLogger();
    
    FUNC_TRACE();

    LOGI("Opening file: " << m_params.url);

    

    auto demuxer = Demuxer::create(m_params.url, m_logger);
    if (!demuxer) {
        LOGE("Failed to create demuxer");
        return false;
    }

    for (int i = 0; i < demuxer->streamCount(); ++i) {
        auto* stream = demuxer->stream(i);
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !m_videoWorker) {
            if (!initializeVideo(demuxer, stream->index)) {
                LOGE("Failed to initialize video stream");
                return false;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !m_audioWorker) {
            if (!initializeAudio(demuxer, stream->index)) {
                LOGE("Failed to initialize audio stream");
                return false;
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
    // seek请求发布后由DemuxerWorker触发(与seek结果无关，seek失败也要唤醒，
    // 否则请求会被压在阻塞的等待之后)：唤醒可能因包队列满而阻塞的解封装线程，
    // 其push会返回Interrupted，由routePacket重判过时性。
    // 顺序约束收在DemuxerWorker内，Controller不需要知道"先seek再interrupt"
    demuxerParams.onSeekRequested = [this]() {
        if (m_videoPacketQueue) {
            m_videoPacketQueue->interrupt();
        }
        if (m_audioPacketQueue) {
            m_audioPacketQueue->interrupt();
        }
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
    if (!m_videoFrameQueue) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        return nullptr;
    }

    return m_videoFrameQueue->pop(clockUsLocked());
}

FramePtr Controller::nextAudioFrame()
{
    if (!m_audioFrameQueue) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clock.mutex);
    if (m_clock.paused) {
        return nullptr;
    }

    auto frame = m_audioFrameQueue->pop(clockUsLocked());
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
    // 不阻塞UI线程；seek失败时serial不递增、回调不触发，时钟与播放无缝保持原状。
    // 请求发布后对生产侧的唤醒由DemuxerWorker自己触发(见onSeekRequested)，
    // 此处不需要知道任何握手顺序
    m_demuxerWorker->seek(-1, targetUs); // -1=容器默认流，pts为AV_TIME_BASE单位
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
    if (m_videoFrameQueue) {
        stats.video.droppedFrames = m_videoFrameQueue->droppedFrames();
    }
    if (m_videoWorker) {
        stats.video.decodedFrames = m_videoWorker->decodedFrames();
    }
    return stats;
}

void Controller::onPacketRead(controller::PacketPtr&& packet)
{
    if (packet->type() == controller::Packet::PacketType::End) {
        // 流结束，音视频队列都要通知到
        if (m_videoPacketQueue) {
            routePacket(*m_videoPacketQueue, Packet::create(packet->serial(), controller::Packet::PacketType::End));
        }
        if (m_audioPacketQueue) {
            routePacket(*m_audioPacketQueue, Packet::create(packet->serial(), controller::Packet::PacketType::End));
        }
    }
    if (m_videoPacketQueue && m_videoPacketQueue->streamIndex() == packet->streamIndex()) {
        routePacket(*m_videoPacketQueue, std::move(packet));
    } else if (m_audioPacketQueue && m_audioPacketQueue->streamIndex() == packet->streamIndex()) {
        routePacket(*m_audioPacketQueue, std::move(packet));
    }
}

void Controller::routePacket(PacketQueue& packetQueue, controller::PacketPtr&& packet)
{
    for (;;) {
        auto result = packetQueue.push(std::move(packet));
        if (result == PacketQueue::PushResult::Pushed || result == PacketQueue::PushResult::Closed) {
            return;
        }
        // Interrupted：队列满时的等待被seek打断。此时重判过时性：
        // 请求已投递则此包读取自旧位置，丢弃它解封装线程才能尽快回到循环顶部消费请求
        if (m_demuxerWorker && m_demuxerWorker->seekPending()) {
            return;
        }
        // 打断与seek无关(如seek成功后清空队列)，继续投递
    }
}

void Controller::onSeekSucceeded(int serial, int64_t targetUs)
{
    // 解封装线程上回调，av_seek_frame已成功、serial已递增。
    // 换代清空在这里统一完成，是"seek成功"唯一的作废落点：解封装线程随后才会
    // readPacket并推入新代次的包，所以清空与推送天然有序，不需要队列自己识别serial
    if (m_videoPacketQueue) {
        m_videoPacketQueue->clear();
    }
    if (m_videoFrameQueue) {
        size_t dropped = m_videoFrameQueue->reset(serial);
        if (dropped > 0) {
            LOGD("Discarded " << dropped << " frames of the previous generation, serial=" << serial);
        }
    }
    if (m_audioPacketQueue) {
        m_audioPacketQueue->clear();
    }
    if (m_audioFrameQueue) {
        size_t dropped = m_audioFrameQueue->reset(serial);
        if (dropped > 0) {
            LOGD("Discarded " << dropped << " audio frames of the previous generation, serial=" << serial);
        }
    }

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
    if (!demuxer->reset()) { // 回退到流的起始位置
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
            // 如果需要转换为软件帧，测试转换是否成功
            if (!containsPixelFormat(m_params.requirePixelFormats, IFrame::PixelFormat::D3D11)) {
                auto hwTransferProcessor = std::make_unique<HWTransferProcessor>(m_logger);
                if (!hwTransferProcessor->process(std::move(frameExp.value()))) {
                    LOGW("Hardware frame transfer to software frame failed");
                    return false;
                }
            }

            return true;
        }
    }
    return false;
}

std::optional<FrameProcessorList> Controller::makeFrameProcessors(const AVStream* stream, DecoderPtr& decoder)
{
    FrameProcessorList processors;

    // 获取首选软件帧格式
    auto preferredPixelFormat = getPreferredSupportedPixelFormat(m_params.requirePixelFormats);
    AVPixelFormat streamSwPixelFormat = AV_PIX_FMT_NONE;

    if (decoder->type() == Decoder::Type::Dxva) {
        // 对于硬解码，如果支持处理硬件帧，不需要任何后处理
        if (containsPixelFormat(m_params.requirePixelFormats, IFrame::PixelFormat::D3D11)) {
            return processors;  // 支持硬件帧，不用任何后处理
        }

        // 否则需要将硬件帧转换为软件帧
        auto hwTransferProcessor = std::make_shared<HWTransferProcessor>(m_logger);
        processors.push_back(hwTransferProcessor);

        streamSwPixelFormat = dxvaDecoderSoftwareFramePixelFormat(stream->codecpar->bits_per_coded_sample);
    } else if (decoder->type() == Decoder::Type::Software) {
        streamSwPixelFormat = static_cast<AVPixelFormat>(stream->codecpar->format);
    }

    if (streamSwPixelFormat == AV_PIX_FMT_NONE) {
        LOGW("Failed to determine software frame pixel format");
        return std::nullopt;
    }

    // 软件帧是否在支持的像素格式列表中，如果在就不用转换软件帧格式
    if (containsPixelFormat(m_params.requirePixelFormats, Frame::toPixelFormat(streamSwPixelFormat))) {
        return processors;
    }

    SwsProcessor::TargetFormat targetFormat;
    targetFormat.format = preferredPixelFormat;
    targetFormat.width = decoder->width();
    targetFormat.height = decoder->height();
    auto swsProcessor = std::make_shared<SwsProcessor>(targetFormat, m_logger);
    processors.push_back(swsProcessor);

    return processors;
}

void Controller::initializeLogger()
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
}

bool Controller::initializeVideo(DemuxerPtr& demuxer, int streamIndex)
{
    const AVStream* stream = demuxer->stream(streamIndex);
    auto videoDecoder = createVideoDecoder(stream, demuxer);
    if (!videoDecoder) {
        LOGE("Failed to create Video Decoder");
        return false;
    }

    DecodeWorker::Params videoDecodeParams{};
    videoDecodeParams.stream = stream;
    videoDecodeParams.logger = m_logger;

    auto processorList = makeFrameProcessors(stream, videoDecoder);
    if (!processorList) {
        LOGE("Failed to create frame processors");
        return false;
    }
    videoDecodeParams.frameProcessors = std::move(*processorList);

    // 包队列：解封装线程 → 解码线程。要根据时长计算队列长度
    double fps = av_q2d(stream->avg_frame_rate);
    fps = fps > 0 ? fps : av_q2d(stream->r_frame_rate);
    fps = fps > 0 ? fps : 30.0; // 默认帧率为30
    PacketQueue::Params videoPacketQueueParams{};
    videoPacketQueueParams.streamIndex = stream->index;
    videoPacketQueueParams.maxDepth = static_cast<size_t>(TARGET_VIDEO_QUEUE_DURATION * fps);
    m_videoPacketQueue = PacketQueue::create(videoPacketQueueParams);
    if (!m_videoPacketQueue) {
        LOGE("Failed to create Video PacketQueue");
        return false;
    }

    // 帧队列：解码线程 → 消费线程。视频允许丢帧：出队侧按主时钟丢弃被覆盖的到期帧
    FrameQueue::Params videoFrameQueueParams{};
    videoFrameQueueParams.canDropFrames = true;
    m_videoFrameQueue = FrameQueue::create(videoFrameQueueParams);
    if (!m_videoFrameQueue) {
        LOGE("Failed to create Video FrameQueue");
        return false;
    }

    videoDecodeParams.packetQueue = m_videoPacketQueue.get();
    videoDecodeParams.frameQueue = m_videoFrameQueue.get();
    m_videoWorker = DecodeWorker::create(videoDecodeParams, std::move(videoDecoder));
    if (!m_videoWorker) {
        LOGE("Failed to create Video DecodeWorker");
        return false;
    }
    return true;
}

bool Controller::initializeAudio(DemuxerPtr& demuxer, int streamIndex)
{
    const AVStream* stream = demuxer->stream(streamIndex);
    SwrProcessor::TargetFormat targetFormat{};
    targetFormat.format = AV_SAMPLE_FMT_S16;
    std::shared_ptr<SwrProcessor> swrProcessor = std::make_shared<SwrProcessor>(targetFormat, m_logger);

    DecodeWorker::Params audioDecodeParams{};
    audioDecodeParams.stream = stream;
    audioDecodeParams.logger = m_logger;
    audioDecodeParams.frameProcessors.push_back(swrProcessor);
    // 包队列：解封装线程 → 解码线程。要根据时长计算队列长度：
    // 包速率 = 采样率 / 每包样本数(frame_size)，部分编码frame_size未知时按1024(AAC典型值)估算
    double sampleRate = stream->codecpar->sample_rate;
    sampleRate = sampleRate > 0 ? sampleRate : 44100; // 默认采样率为44100
    double frameSize = stream->codecpar->frame_size;
    frameSize = frameSize > 0 ? frameSize : 1024.0;
    PacketQueue::Params audioPacketQueueParams{};
    audioPacketQueueParams.streamIndex = stream->index;
    audioPacketQueueParams.maxDepth = std::max<size_t>(
        static_cast<size_t>(TARGET_AUDIO_QUEUE_DURATION * sampleRate / frameSize), 16);
    m_audioPacketQueue = PacketQueue::create(audioPacketQueueParams);
    if (!m_audioPacketQueue) {
        LOGE("Failed to create Audio PacketQueue");
        return false;
    }

    // 帧队列：解码线程 → 消费线程。音频不丢帧，且不做严格时钟门控，按阈值放行
    FrameQueue::Params audioFrameQueueParams{};
    audioFrameQueueParams.canDropFrames = false;
    audioFrameQueueParams.controlClock = false;
    m_audioFrameQueue = FrameQueue::create(audioFrameQueueParams);
    if (!m_audioFrameQueue) {
        LOGE("Failed to create Audio FrameQueue");
        return false;
    }

    audioDecodeParams.packetQueue = m_audioPacketQueue.get();
    audioDecodeParams.frameQueue = m_audioFrameQueue.get();

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
    return true;
}

} // namespace controller