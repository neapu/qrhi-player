#pragma once
#include "media/IMediaSource.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    static std::unique_ptr<MediaSource> create(const Params& params);

    ~MediaSource() override;

    int start(bool audioAvailable) override;

    MediaFrame nextVideoFrame() override;
    MediaFrame nextAudioFrame() override;

    std::optional<VideoParams> videoParams() override;
    std::optional<AudioParams> audioParams() override;

    int64_t duration() override;
    void seek(int64_t positionUs) override;

    Statistics statistics() override;
private:
    explicit MediaSource(const Params& params);
    bool initialize();


private:
    std::string m_source{};
    std::string m_logDir{};
    std::string m_instanceName{};
    std::vector<AVPixelFormat> m_requiredPixelFormats{};
    std::vector<AVSampleFormat> m_requiredSampleFormats{};
    std::function<void(bool, int)> m_onSeekCompleted{};
};

} // namespace media