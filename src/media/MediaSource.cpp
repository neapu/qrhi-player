#include "MediaSource.h"
#include "Demuxer.h"
#include "SwsProcessor.h"
#include "SwrProcessor.h"

namespace {
bool containsPixelFormat(const std::vector<AVPixelFormat>& formats, AVPixelFormat format)
{
    return std::find(formats.begin(), formats.end(), format) != formats.end();
}

bool containsSampleFormat(const std::vector<AVSampleFormat>& formats, AVSampleFormat format)
{
    return std::find(formats.begin(), formats.end(), format) != formats.end();
}

}

namespace media {
std::unique_ptr<IMediaSource> IMediaSource::create(const IMediaSource::Params& params)
{
    return MediaSource::create(params);
}

std::unique_ptr<MediaSource> MediaSource::create(const IMediaSource::Params& params)
{
    auto instance = std::unique_ptr<MediaSource>(new MediaSource(params));
    if (!instance->initialize()) {
        return nullptr;
    }
    return instance;
}

MediaSource::MediaSource(const IMediaSource::Params& params)
    : m_source(params.source)
    , m_logDir(params.logDir)
    , m_instanceName(params.instanceName)
    , m_requiredPixelFormats(params.requiredPixelFormats)
    , m_requiredSampleFormats(params.requiredSampleFormats)
    , m_initialSerial(params.initialSerial)
{
    if (m_instanceName.empty()) {
        m_instanceName = "default";
    }
} 

MediaSource::~MediaSource()
{
    if (m_videoDecodeWorker) {
        m_videoDecodeWorker->stop();
    }
    if (m_audioDecodeWorker) {
        m_audioDecodeWorker->stop();
    }
    if (m_demuxWorker) {
        m_demuxWorker->stop();
    }

    if (m_logger) {
        m_logger->flush();
        spdlog::drop(m_instanceName);
    }
}

bool MediaSource::initialize()
{
    initializeLogger();

    FUNC_TRACE(m_logger, spdlog::level::info);

    DemuxerPtr demuxer = Demuxer::create({
        m_source,
        m_logger,
    });
    if (!demuxer) {
        LOG_ERROR(m_logger, "Failed to create demuxer");
        return false;
    }
    m_duration = demuxer->duration();

    auto streamCount = demuxer->streamCount();
    // 现在先选择第一个视频流和第一个音频流
    for (uint32_t i = 0; i < streamCount; ++i) {
        auto stream = demuxer->stream(i);
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !m_audioDecodeWorker) {
            if (!initializeAudio(stream)) {
                LOG_ERROR(m_logger, "Failed to initialize audio stream at index {}", i);
                return false;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !m_videoDecodeWorker) {
            if (!initializeVideo(stream)) {
                LOG_ERROR(m_logger, "Failed to initialize video stream at index {}", i);
                return false;
            }
        }
    }
    if (!m_videoDecodeWorker && !m_audioDecodeWorker) {
        // 至少要有音频或视频
        LOG_ERROR(m_logger, "Failed to initialize any decode worker");
        return false;
    }

    DemuxWorker::Params demuxWorkerParams{
        m_logger,
        m_audioDecodeWorker ? m_audioDecodeWorker->streamIndex() : m_videoDecodeWorker->streamIndex(),
        m_initialSerial
    };
    m_demuxWorker = DemuxWorker::create(demuxWorkerParams, std::move(demuxer));
    if (!m_demuxWorker) {
        LOG_ERROR(m_logger, "Failed to create demux worker");
        return false;
    }

    // 工作线程延后到 start() 启动：主流的选择依赖音频渲染器的创建结果
    return true;
}

void MediaSource::start(bool audioAvailable)
{
    FUNC_TRACE(m_logger, spdlog::level::info);

    if (m_started) {
        LOG_WARN(m_logger, "start called twice, ignored");
        return;
    }

    // 音频没有消费端（如音频设备打开失败）时不启动音频解码线程，
    // 并改选视频流为主流，否则无人消费的音频队列会阻塞整个解封装链路
    m_audioActive = audioAvailable && m_audioDecodeWorker;
    if (!m_audioActive && m_videoDecodeWorker) {
        m_demuxWorker->setMainStream(m_videoDecodeWorker->streamIndex());
    }

    m_demuxWorker->start();
    if (m_videoDecodeWorker) {
        m_videoDecodeWorker->start();
    }
    if (m_audioActive) {
        m_audioDecodeWorker->start();
    }
    m_started = true;
}

MediaFrame MediaSource::nextVideoFrame()
{
    return m_videoDecodeWorker ? m_videoDecodeWorker->nextFrame() : EmptyFrame{};
}

MediaFrame MediaSource::nextAudioFrame()
{
    return m_audioDecodeWorker ? m_audioDecodeWorker->nextFrame() : EmptyFrame{};
}

std::optional<IMediaSource::AudioParams> MediaSource::audioParams()
{
    if (!m_audioDecodeWorker) {
        return std::nullopt;
    }
    return m_audioParams;
}

std::optional<IMediaSource::VideoParams> MediaSource::videoParams()
{
    if (!m_videoDecodeWorker) {
        return std::nullopt;
    }
    return m_videoParams;
}

int64_t MediaSource::duration()
{
    return m_duration;
}

int MediaSource::seek(int64_t timestamp)
{
    int serial{0};
    if (m_demuxWorker) {
        serial = m_demuxWorker->seek(timestamp);
    }
    if (m_videoDecodeWorker) {
        m_videoDecodeWorker->seekRequired();
    }
    if (m_audioDecodeWorker) {
        m_audioDecodeWorker->seekRequired();
    }
    return serial;
}

void MediaSource::initializeLogger()
{
    if (m_logger) {
        return;
    }

    m_logger = createLogger(m_instanceName, m_logDir);

    LOG_INFO(m_logger, "Open media: {}", m_source);
}

bool MediaSource::initializeVideo(const AVStream* stream)
{
    if (!stream) {
        LOG_ERROR(m_logger, "Video stream is null");
        return false;
    }
    
    Decoder::Params videoDecoderParams{};
    videoDecoderParams.stream = stream;
    videoDecoderParams.logger = m_logger;
    auto decoder = Decoder::create(videoDecoderParams);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create video decoder");
        return false;
    }

    uint32_t videoStreamIndex = stream->index;
    DecodeWorker::Params videoDecodeWorkerParams{};
    videoDecodeWorkerParams.logger = m_logger;
    videoDecodeWorkerParams.nextPacketCallback = [this, videoStreamIndex]() {
        return m_demuxWorker->nextPacket(videoStreamIndex);
    };
    auto videoFrameProcessorsRet = makeVideoFrameProcessors(decoder);
    if (!videoFrameProcessorsRet) {
        LOG_ERROR(m_logger, "Failed to create video frame processors");
        return false;
    }
    videoDecodeWorkerParams.frameProcessors = std::move(*videoFrameProcessorsRet);
    videoDecodeWorkerParams.initialSerial = m_initialSerial;

    m_videoParams.width = decoder->width();
    m_videoParams.height = decoder->height();
    m_videoParams.pixelFormat = m_targetPixelFormat;
    m_videoParams.timeBase = decoder->timeBase();
    m_videoParams.frameRate = decoder->frameRate();
    m_videoDecodeWorker = DecodeWorker::create(videoDecodeWorkerParams, std::move(decoder));
    if (!m_videoDecodeWorker) {
        LOG_ERROR(m_logger, "Failed to create video decode worker");
        return false;
    }
    
    return true;
}

bool MediaSource::initializeAudio(const AVStream* stream)
{
    if (!stream) {
        LOG_ERROR(m_logger, "Audio stream is null");
        return false;
    }

    Decoder::Params audioDecoderParams{};
    audioDecoderParams.stream = stream;
    audioDecoderParams.logger = m_logger;
    auto decoder = Decoder::create(audioDecoderParams);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create audio decoder");
        return false;
    }

    uint32_t audioStreamIndex = stream->index;
    DecodeWorker::Params audioDecodeWorkerParams{};
    audioDecodeWorkerParams.logger = m_logger;
    audioDecodeWorkerParams.nextPacketCallback = [this, audioStreamIndex]() {
        return m_demuxWorker->nextPacket(audioStreamIndex);
    };
    auto audioFrameProcessorsRet = makeAudioFrameProcessors(decoder);
    if (!audioFrameProcessorsRet) {
        LOG_ERROR(m_logger, "Failed to create audio frame processors");
        return false;
    }
    audioDecodeWorkerParams.frameProcessors = std::move(*audioFrameProcessorsRet);
    audioDecodeWorkerParams.initialSerial = m_initialSerial;

    m_audioParams.sampleRate = decoder->sampleRate();
    m_audioParams.channels = decoder->chLayout().nb_channels;
    m_audioParams.sampleFormat = m_targetSampleFormat;
    m_audioParams.timeBase = decoder->timeBase();
    m_audioDecodeWorker = DecodeWorker::create(audioDecodeWorkerParams, std::move(decoder));
    if (!m_audioDecodeWorker) {
        LOG_ERROR(m_logger, "Failed to create audio decode worker");
        return false;
    }
    
    return true;
}

std::optional<FrameProcessorList> MediaSource::makeVideoFrameProcessors(const DecoderPtr& decoder)
{
    FrameProcessorList frameProcessors;
    auto pixelFormat = decoder->pixelFormat();
    if (pixelFormat == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "Failed to get video pixel format");
        return std::nullopt;
    }

    if (containsPixelFormat(m_requiredPixelFormats, pixelFormat)) {
        m_targetPixelFormat = pixelFormat;
        return frameProcessors; // 像素格式再请求的格式列表中，不用转换
    }

    // 获取第一个请求的软件像素格式
    AVPixelFormat targetPixelFormat{AV_PIX_FMT_NONE};
    for (auto fmt : m_requiredPixelFormats) {
        if (fmt != AV_PIX_FMT_NONE) {
            targetPixelFormat = fmt;
            break;
        }
    }
    if (targetPixelFormat == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "No valid target pixel format found");
        return std::nullopt;
    }

    SwsProcessor::Params swsProcessorParams{};
    swsProcessorParams.logger = m_logger;
    swsProcessorParams.width = decoder->width();
    swsProcessorParams.height = decoder->height();
    swsProcessorParams.pixelFormat = targetPixelFormat;
    m_targetPixelFormat = targetPixelFormat;

    auto swsProcessor = SwsProcessor::create(swsProcessorParams);
    if (!swsProcessor) {
        LOG_ERROR(m_logger, "Failed to create SwsProcessor");
        return std::nullopt;
    }
    frameProcessors.push_back(std::move(swsProcessor));

    return frameProcessors;
}

std::optional<FrameProcessorList> MediaSource::makeAudioFrameProcessors(const DecoderPtr& decoder)
{
    FrameProcessorList frameProcessors;
    auto sampleFormat = decoder->sampleFormat();
    if (sampleFormat == AV_SAMPLE_FMT_NONE) {
        LOG_ERROR(m_logger, "Failed to get audio sample format");
        return std::nullopt;
    }

    if (containsSampleFormat(m_requiredSampleFormats, sampleFormat)) {
        m_targetSampleFormat = sampleFormat;
        return frameProcessors; // 样本格式在请求的格式列表中，不用转换
    }

    // 获取第一个请求的软件样本格式
    AVSampleFormat targetSampleFormat{AV_SAMPLE_FMT_NONE};
    for (auto fmt : m_requiredSampleFormats) {
        if (fmt != AV_SAMPLE_FMT_NONE) {
            targetSampleFormat = fmt;
            break;
        }
    }
    if (targetSampleFormat == AV_SAMPLE_FMT_NONE) {
        LOG_ERROR(m_logger, "No valid target sample format found");
        return std::nullopt;
    }

    SwrProcessor::Params swrProcessorParams{};
    swrProcessorParams.logger = m_logger;
    swrProcessorParams.sampleFormat = targetSampleFormat;
    m_targetSampleFormat = targetSampleFormat;
    swrProcessorParams.sampleRate = decoder->sampleRate();
    av_channel_layout_copy(&swrProcessorParams.channelLayout, &decoder->chLayout());

    auto swrProcessor = SwrProcessor::create(swrProcessorParams);
    if (!swrProcessor) {
        LOG_ERROR(m_logger, "Failed to create SwrProcessor");
        return std::nullopt;
    }
    frameProcessors.push_back(std::move(swrProcessor));

    return frameProcessors;
}

} // namespace media