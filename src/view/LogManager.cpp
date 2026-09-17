#include "LogManager.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <QDir>
#include <QStandardPaths>

// 防御：DEBUG 已定义但 SOURCE_DIR 缺失时（如手动编译）退化为当前目录
#ifndef SOURCE_DIR
#define SOURCE_DIR "."
#endif

namespace {
constexpr auto MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
constexpr auto MAX_FILES = 5; // 最大保留的日志文件数量

// Debug 写到源码目录的 logs/ 便于开发查看；Release 写到 %APPDATA%/<应用名>/logs，
// AppDataLocation 依赖 QCoreApplication::applicationName()，需在 LogManager 初始化前创建 QApplication
QString logDirectory()
{
#ifdef DEBUG
    const QString dir = QString::fromUtf8(SOURCE_DIR) + QStringLiteral("/logs");
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
#endif
    if (!QDir().mkpath(dir)) {
        // 目录创建失败时退到临时目录，避免文件 sink 构造抛异常导致程序无法启动
        return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return dir;
}

spdlog::filename_t toFilenameT(const QString& path)
{
#ifdef SPDLOG_WCHAR_FILENAMES
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}
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
    const QString logDir = logDirectory();
    m_mainLogger = createModuleLogger("main", toFilenameT(logDir + QStringLiteral("/main.log")));
    m_controllerLogger = createModuleLogger("controller", toFilenameT(logDir + QStringLiteral("/controller.log")));
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