#include "LogManager.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {
constexpr auto MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
constexpr auto MAX_FILES = 5; // 最大保留的日志文件数量
// 注意：WIN32 下定义了 SPDLOG_WCHAR_FILENAMES，spdlog::filename_t 是 std::wstring，
// 因此文件名参数必须用 spdlog::filename_t / SPDLOG_FILENAME_T 才能跨平台一致
std::shared_ptr<spdlog::logger> createModuleLogger(const std::string& moduleName, const spdlog::filename_t& logFileName)
{
    // 创建主程序logger
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFileName, 
        MAX_FILE_SIZE,
        MAX_FILES
    );

#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(moduleName, spdlog::sinks_init_list({fileSink, consoleSink}));
    logger->set_level(spdlog::level::debug);
#else
    auto logger = std::make_shared<spdlog::logger>(moduleName, spdlog::sinks_init_list({fileSink}));
    logger->set_level(spdlog::level::info);
#endif
    logger->flush_on(spdlog::level::warn); // warning以上级别的日志立即刷新，避免程序崩溃时日志丢失
    // 设置日志格式
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    return logger;
}

spdlog::level::level_enum qtMsgTypeToSpdlogLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return spdlog::level::debug;
    case QtInfoMsg:
        return spdlog::level::info;
    case QtWarningMsg:
        return spdlog::level::warn;
    case QtCriticalMsg:
        return spdlog::level::critical;
    case QtFatalMsg:
        return spdlog::level::err;
    default:
        return spdlog::level::info;
    }
}

spdlog::level::level_enum controllerLogLevelToSpdlogLevel(controller::LogLevel level)
{
    switch (level) {
    case controller::LogLevel::Debug:
        return spdlog::level::debug;
    case controller::LogLevel::Info:
        return spdlog::level::info;
    case controller::LogLevel::Warning:
        return spdlog::level::warn;
    case controller::LogLevel::Error:
        return spdlog::level::err;
    case controller::LogLevel::Fatal:
        return spdlog::level::critical;
    default:
        return spdlog::level::info;
    }
}
} // namespace

namespace view {
LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance;
}

LogManager::LogManager()
{
    m_mainLogger = createModuleLogger("main", SPDLOG_FILENAME_T("main.log"));
    m_controllerLogger = createModuleLogger("controller", SPDLOG_FILENAME_T("controller.log"));
}

void LogManager::logQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    m_mainLogger->log(
        spdlog::source_loc{
            context.file,
            context.line,
            context.function
        },
        qtMsgTypeToSpdlogLevel(type),
        message.toStdString()
    );
}

void LogManager::logControllerMessage(controller::LogLevel level, const std::string& fileName, int line, const std::string& message)
{
    m_controllerLogger->log(
        spdlog::source_loc{
            fileName.c_str(),
            line,
            ""
        },
        controllerLogLevelToSpdlogLevel(level),
        message
    );
}

void LogManager::shutdown()
{
    spdlog::shutdown();
}

} // namespace view