#pragma once
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <Frame.h>

namespace media {
class IMediaSource {
public:
    struct Params {
        std::string source{};
        std::string logDir{};
        std::string instanceName{"default"};   // 多实例时区分日志
        std::vector<AVPixelFormat> requiredPixelFormats{};
        std::vector<AVSampleFormat> requiredSampleFormats{};
    };

    static std::unique_ptr<IMediaSource> create(const Params& params);

    virtual ~IMediaSource() = default;

    /**
     * @brief 获取下一个视频帧。消费操作。
     * @return 下一个视频帧，如果没有则返回空。
     */
    virtual fh::FramePtr nextVideoFrame() = 0;
    /**
     * @brief 获取下一个音频帧。消费操作。
     * @return 下一个音频帧，如果没有则返回空。
     */
    virtual fh::FramePtr nextAudioFrame() = 0;

    /**
     * @brief 判断媒体是否播放到结尾。
     * @return 如果已经播放到结尾，返回true，否则返回false。
     */
    virtual bool endOfFile() = 0;

    struct VideoParams {
        int width; // 视频宽度
        int height; // 视频高度
        AVPixelFormat pixelFormat; // 像素格式
        AVRational timeBase; // 时间基
        AVRational frameRate; // 帧率
    };
    /**
     * @brief 获取视频参数。
     * @return 如果有视频流，返回视频参数，否则返回空。
     */
    virtual std::optional<VideoParams> videoParams() = 0;

    struct AudioParams {
        int sampleRate; // 采样率
        int channels; // 通道数
        AVSampleFormat sampleFormat; // 采样格式
        AVRational timeBase; // 时间基
    };
    /**
     * @brief 获取音频参数。
     * @return 如果有音频流，返回音频参数，否则返回空。
     */
    virtual std::optional<AudioParams> audioParams() = 0;

    /**
     * @brief 获取媒体的总时长，单位为微秒。
     * @return 媒体的总时长，单位为微秒。
     */
    virtual int64_t duration() = 0;
    /**
     * @brief 跳转到指定的时间戳，单位为微秒。
     * @param timestamp 目标时间戳，单位为微秒。
     */
    virtual void seek(int64_t timestamp) = 0;
};
using MediaSourcePtr = std::unique_ptr<IMediaSource>;
} // namespace media