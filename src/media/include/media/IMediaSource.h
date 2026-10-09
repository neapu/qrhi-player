#pragma once
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "MediaFrame.h"
#include "Statistics.h"

namespace media {
enum class Error {
    /**
     * @brief 读取媒体数据失败。
     * @note 恢复方法：触发一次成功的 seek 操作。
     */
    ReadFailed,
    // 根据后续实现添加错误类型

    /**
     * @brief 未知错误。
     * @note 所有未知错误都视为不可恢复，需要重建 MediaSource 实例。
     */
    Unknown
};
class IMediaSource {
public:
    struct Params {
        std::string source{};
        std::string logDir{};
        std::string instanceName{"default"};   // 多实例时区分日志
        std::vector<AVPixelFormat> requiredPixelFormats{};
        std::vector<AVSampleFormat> requiredSampleFormats{};
        /**
         * @brief 回调函数，在 seek 操作完成后被调用。
         * @param success 是否成功完成 seek 操作。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(bool)> onSeekCompleted;
        /**
         * @brief 回调函数，在媒体源发生错误时被调用。
         * @param error 错误类型。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(Error)> onError;
    };

    static std::unique_ptr<IMediaSource> create(const Params& params);

    virtual ~IMediaSource() = default;

    struct StartOptions {
        bool audioAvailable{false}; // 是否存在音频消费端（音频渲染器是否创建成功）
        bool videoAvailable{false}; // 是否存在视频消费端（视频渲染器是否创建成功）
    };
    /**
     * @brief 启动内部工作线程（解封装、解码）。
     * @param audioAvailable 是否存在音频消费端（音频渲染器是否创建成功）。
     *        为 false 时：若文件有视频流，视频流成为主流且音频解码线程不启动
     *        （音频设备打开失败时降级为无声视频播放）；
     *        若文件无视频流则仍以音频流为主流。
     * @note 工作线程将在析构时停止，析构回调用 join，不能在回调中析构 MediaSource 实例。
     *       create 之后必须调用一次 start，nextVideoFrame/nextAudioFrame 才会产出数据。
     *       MediaSource 实例生命周期中只能 start 一次。
     */
    virtual void start(const StartOptions& options) = 0;

    /**
     * @brief 获取下一个视频帧。消费操作。
     * @return 下一个视频帧，如果度到流结尾，返回 EndFrame
     * @note 非阻塞，如果帧队列为空，返回 EmptyFrame。
     *       线程安全。
     */
    virtual MediaFrame nextVideoFrame() = 0;
    /**
     * @brief 获取下一个音频帧。消费操作。
     * @return 下一个音频帧，如果度到流结尾，返回 EndFrame
     * @note 非阻塞，如果帧队列为空，返回 EmptyFrame。
     *       线程安全。
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
     * @param positionUs 目标时间戳，单位为微秒。
     * @note 完成结果通过 Params::onSeekCompleted 回调报告。
     */
    virtual void seek(int64_t positionUs) = 0;

    /**
     * @brief 获取媒体的统计信息。
     */
    virtual Statistics statistics() = 0;
};
using MediaSourcePtr = std::unique_ptr<IMediaSource>;
} // namespace media