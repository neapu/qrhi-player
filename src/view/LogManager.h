#pragma once
#include <functional>
#include <spdlog/spdlog.h>
#include <QString>
#include <QtGlobal>
#include "controller/Controller.h"

namespace view {
class LogManager {
public:
    static LogManager& instance();

    void logQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message);
    void logControllerMessage(controller::LogLevel level, const std::string& fileName, int line, const std::string& message);

    static void shutdown();
private:
    LogManager();

private:
    std::shared_ptr<spdlog::logger> m_mainLogger;
    std::shared_ptr<spdlog::logger> m_controllerLogger;
};

} // namespace view