#pragma once
#include "media/IMediaSource.h"
#include "Logger.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    static std::unique_ptr<MediaSource> create(const IMediaSource::Params& params);

    virtual ~MediaSource();

    fh::FramePtr nextVideoFrame() override;
    fh::FramePtr nextAudioFrame() override;
    bool endOfFile() override;
    std::optional<AudioParams> audioParams() override;
    int64_t duration() override;
    void seek(int64_t timestamp) override;
private:
    explicit MediaSource(const IMediaSource::Params& params);
    bool initialize();
    void initializeLogger();

private:
    std::string m_source{};
    std::string m_logDir{};
    std::string m_instanceName{};
    std::vector<AVPixelFormat> m_requiredPixelFormats{};
    std::vector<AVSampleFormat> m_requiredSampleFormats{};

    LoggerPtr m_logger;
};

} // namespace media