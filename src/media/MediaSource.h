#pragma once
#include "media/IMediaSource.h"
#include "Logger.h"
#include "DemuxWorker.h"
#include "DecodeWorker.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    static std::unique_ptr<MediaSource> create(const IMediaSource::Params& params);

    virtual ~MediaSource();

    fh::FramePtr nextVideoFrame() override;
    fh::FramePtr nextAudioFrame() override;
    bool endOfFile() override;
    std::optional<AudioParams> audioParams() override;
    std::optional<VideoParams> videoParams() override;
    int64_t duration() override;
    void seek(int64_t timestamp) override;
private:
    explicit MediaSource(const IMediaSource::Params& params);
    bool initialize();
    void initializeLogger();

    bool initializeVideo(const AVStream* stream);
    bool initializeAudio(const AVStream* stream);

    std::optional<FrameProcessorList> makeVideoFrameProcessors(const DecoderPtr& decoder);
    std::optional<FrameProcessorList> makeAudioFrameProcessors(const DecoderPtr& decoder);
private:
    std::string m_source{};
    std::string m_logDir{};
    std::string m_instanceName{};
    std::vector<AVPixelFormat> m_requiredPixelFormats{};
    std::vector<AVSampleFormat> m_requiredSampleFormats{};

    LoggerPtr m_logger;

    DemuxWorkerPtr m_demuxWorker{nullptr};
    DecodeWorkerPtr m_videoDecodeWorker{nullptr};
    DecodeWorkerPtr m_audioDecodeWorker{nullptr};

    VideoParams m_videoParams{};
    AudioParams m_audioParams{};
    int64_t m_duration{0};
};

} // namespace media