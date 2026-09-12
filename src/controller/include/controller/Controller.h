#pragma once
#include <memory>
#include <string>
#include <functional>
#include "export.h"
#include "Frame.h"

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
     * @brief 获取下一帧视频帧
     * @return FramePtr 下一帧视频帧
     */
    virtual FramePtr nextVideoFrame() const = 0;
    /**
     * @brief 获取下一帧音频帧
     * @return FramePtr 下一帧音频帧
     */
    virtual FramePtr nextAudioFrame() const = 0;

    /**
     * @brief 获取媒体的总时长
     * @return double 媒体的总时长，单位为秒
     */
    virtual double duration() const = 0;
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
};
using ControllerPtr = std::unique_ptr<IController>;
} // controller namespace