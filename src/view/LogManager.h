#pragma once
#include <QString>
#include <spdlog/spdlog.h>

namespace view {
class LogManager {
public:
    static LogManager& instance();
    static QString logDir();

    void initialize();
    void shutdown();

private:
    static void logQtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

private:
    std::shared_ptr<spdlog::logger> m_logger{nullptr};
    std::shared_ptr<spdlog::logger> m_ffmpegLogger{nullptr};
};


} // namespace view