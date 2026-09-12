#pragma once
#include <QString>
#include "controller/Controller.h"

namespace view {

/**
 * @brief 日志管理单例：初始化 spdlog、安装 Qt 消息处理器，统一管理各模块的日志输出。
 *        主程序与 Controller 模块分别写入独立的日志文件（application.log / controller.log），
 *        Debug 构建额外输出到控制台。首次 instance() 时接管 Qt 日志，进程退出时析构并关闭日志系统。
 */
class LogManager {
public:
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    static LogManager& instance();

    /**
     * @brief 获取 Controller 模块使用的日志回调，输出到独立的 controller 日志文件
     * @return controller::LogCallback 日志回调
     */
    controller::LogCallback controllerLogCallback() const;

private:
    LogManager();
    ~LogManager();
};

} // namespace view
