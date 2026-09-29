#pragma once
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "MediaFrame.h"
#include "Statistics.h"

namespace media {
class IMediaSource {
public:
    struct Params {
        std::string source{};
        std::string logDir{};
        std::string instanceName{"default"};   // 多实例时区分日志
        std::vector<AVPixelFormat> requiredPixelFormats{};
        std::vector<AVSampleFormat> requiredSampleFormats{};
        int initialSerial{0};
        std::function<void(bool, int)> onSeekCompleted;
    };

    static std::unique_ptr<IMediaSource> create(const Params& params);

    virtual ~IMediaSource() = default;

    /**
     * @brief 启动内部工作线程（解封装、解码）。create 之后必须调用一次，
     *        之后 nextVideoFrame/nextAudioFrame 才会产出数据。
     * @param audioAvailable 是否存在音频消费端（音频渲染器是否创建成功）。
     *        为 false 时：若文件有视频流，视频流成为主流且音频解码线程不启动
     *        （音频设备打开失败时降级为无声视频播放）；
     *        若文件无视频流则仍以音频流为主流。
     */
    virtual void start(bool audioAvailable) = 0;

    /**
     * @brief 获取下一个视频帧。消费操作。
     * @return 下一个视频帧，如果度到流结尾，返回 EndFrame
     * @note 非阻塞，如果帧队列为空，返回 EmptyFrame。
     */
    virtual MediaFrame nextVideoFrame() = 0;
    /**
     * @brief 获取下一个音频帧。消费操作。
     * @return 下一个音频帧，如果度到流结尾，返回 EndFrame
     * @note 非阻塞，如果帧队列为空，返回 EmptyFrame。
     */
    virtual MediaFrame nextAudioFrame() = 0;

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
     * @note 完成结果通过 Params::onSeekCompleted 回调报告。
     */
    virtual void seek(int64_t timestamp) = 0;

    /**
     * @brief 获取媒体的统计信息。
     */
    virtual Statistics statistics() = 0;
};
using MediaSourcePtr = std::unique_ptr<IMediaSource>;
} // namespace media