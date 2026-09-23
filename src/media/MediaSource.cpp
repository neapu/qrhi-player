#include "MediaSource.h"

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
{
    if (m_instanceName.empty()) {
        m_instanceName = "default";
    }
} 

MediaSource::~MediaSource()
{
    if (m_logger) {
        m_logger->flush();
    }
}

bool MediaSource::initialize()
{
    initializeLogger();

    FUNC_TRACE(m_logger, spdlog::level::info);
    
    return true;
}

fh::FramePtr MediaSource::nextVideoFrame()
{
    return nullptr;
}

fh::FramePtr MediaSource::nextAudioFrame()
{
    return nullptr;
}

bool MediaSource::endOfFile()
{
    return false;
}

std::optional<IMediaSource::AudioParams> MediaSource::audioParams()
{
    return std::nullopt;
}

int64_t MediaSource::duration()
{
    return 0;
}

void MediaSource::seek(int64_t timestamp)
{
}

void MediaSource::initializeLogger()
{
    if (m_logger) {
        return;
    }

    m_logger = createLogger(m_instanceName, m_logDir);
}

} // namespace media