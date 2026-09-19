#pragma once
#include <memory>
#include <optional>
#include <string>
#include <functional>
#include "export.h"
#include "Frame.h"
#include "Statistics.h"

namespace controller {
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};
using LogCallback = std::function<void(LogLevel, const std::string& fileName, int line, const std::string& message)>;

/**
 * @brief 媒体控制器接口，负责打开媒体，解码音视频帧，管理播放状态；不负责渲染
 *        一个实例对应一个媒体源，符合RAII原则。
 */
class IController {
public:
    struct Params {
        std::string url;
        LogCallback logCallback;
    };
    static CONTROLLER_EXPORT std::unique_ptr<IController> create(const Params& params);

    virtual ~IController() = default;

    /**
     * @brief 获取下一帧视频帧。消费性操作：会推进内部队列与丢帧状态
     * @return FramePtr 下一帧视频帧
     */
    virtual FramePtr nextVideoFrame() = 0;
    /**
     * @brief 获取下一帧音频帧。消费性操作：会推进内部队列，seek后还会按时钟重锚
     * @return FramePtr 下一帧音频帧
     */
    virtual FramePtr nextAudioFrame() = 0;

    /**
     * @brief 设置正在播放的音频的时间点，由音频渲染侧周期性调用，用于校准时钟
     * @param renderTimeUs 正在播放的音频的时间点，单位为微秒
     * @note 内部按偏差大小分级校准：小偏差忽略，中等偏差以微调速率平滑收敛，大偏差直接对齐
     */
    virtual void audioRenderTime(int64_t renderTimeUs) = 0;

    /**
     * @brief 音频输出参数，即nextAudioFrame交付帧的参数
     */
    struct AudioParams {
        int sampleRate{0};                                          // 采样率(Hz)
        int channels{0};                                            // 声道数
        IFrame::SampleFormat sampleFormat{IFrame::SampleFormat::None}; // 采样格式
    };

    /**
     * @brief 获取音频输出参数。初始化完成后即可调用，供渲染侧提前打开音频设备，
     *        无需等首个音频帧到达
     * @return std::optional<AudioParams> 参数；媒体无音频流时返回空
     */
    virtual std::optional<AudioParams> audioParams() const = 0;

    /**
     * @brief 获取媒体的总时长
     * @return double 媒体的总时长，单位为秒
     */
    virtual double duration() const = 0;
    /**
     * @brief 获取当前播放位置，供UI显示进度
     * @return double 当前播放位置，单位为秒；暂停时为暂停时冻结的位置
     */
    virtual double position() const = 0;
    /**
     * @brief 跳转到指定的时间点
     * @param timepoint 时间点，单位为秒
     */
    virtual void seek(double timepoint) = 0;

    /**
     * @brief 暂停或恢复播放
     */
    virtual void pauseOrResume() = 0;
    /**
     * @brief 判断当前是否处于暂停状态
     * @return bool 是否暂停
     */
    virtual bool isPaused() const = 0;

    virtual StatisticsData statistics() const = 0;
};
using ControllerPtr = std::unique_ptr<IController>;
} // controller namespace