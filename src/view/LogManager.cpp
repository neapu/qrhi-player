#include "LogManager.h"

#include <QDir>
#include <QDebug>
#include <QStandardPaths>

#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#ifdef DEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace view {
namespace {

constexpr char APP_LOGGER_NAME[] = "app";
constexpr char CONTROLLER_LOGGER_NAME[] = "controller";

// 单个文件最大10MB，最多保留5个文件
constexpr size_t MAX_LOG_FILE_SIZE = 10 * 1024 * 1024;
constexpr size_t MAX_LOG_FILES = 5;

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
            return spdlog::level::err;
        case QtFatalMsg:
            return spdlog::level::critical;
        default:
            return spdlog::level::info; // 默认级别
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
            return spdlog::level::info; // 默认级别
    }
}

QString resolveLogDir()
{
#ifdef DEBUG
    return QString(SOURCE_DIR) + "/logs";
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
#endif
}

// spdlog 在 Windows 且定义了 SPDLOG_WCHAR_FILENAMES 时 filename_t 为 std::wstring，
// 能正确打开含非 ASCII 字符（如中文用户名）的 %APPDATA% 路径；否则为 std::string
spdlog::filename_t toLogFilename(const QString& path)
{
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}

void createModuleLogger(const std::string& name, const QString& logFilePath)
{
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        toLogFilename(logFilePath),
        MAX_LOG_FILE_SIZE,
        MAX_LOG_FILES
    );

#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{fileSink, consoleSink});
#else
    auto logger = std::make_shared<spdlog::logger>(name, fileSink);
#endif

    logger->set_level(spdlog::level::debug); // 设置日志级别
    logger->flush_on(spdlog::level::warn);   // 设置立即刷新日志
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v"); // 设置日志格式

    spdlog::register_logger(logger);
}

void logToModule(const std::string& name, spdlog::level::level_enum level, const spdlog::source_loc& location, const std::string& message)
{
    auto logger = spdlog::get(name);
    if (!logger) {
        return; // 日志系统未初始化或已销毁，丢弃日志
    }
    try {
        logger->log(location, level, message);
    } catch (...) {
        // 日志处理函数不能抛异常，否则可能导致程序崩溃
    }
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    logToModule(
        APP_LOGGER_NAME,
        qtMsgTypeToSpdlogLevel(type),
        spdlog::source_loc{
            context.file ? context.file : "",
            context.line,
            context.function ? context.function : ""
        },
        msg.toStdString()
    );

    if (type == QtFatalMsg) {
        std::abort(); // Qt 约定致命消息的处理器不得返回；QtFatalMsg 已映射为 critical，flush_on(warn) 保证落盘
    }
}

} // namespace

LogManager& LogManager::instance()
{
    static LogManager instance; // Meyers 单例：C++11 起初始化线程安全，进程退出时自动析构
    return instance;
}

LogManager::LogManager()
{
    const QString logDir = resolveLogDir();
    if (!QDir(logDir).exists()) {
        if (!QDir().mkpath(logDir)) {
            qWarning() << "Failed to create log directory:" << logDir; // 处理器尚未安装，输出到控制台
            return;
        }
    }

    try {
        createModuleLogger(APP_LOGGER_NAME, logDir + "/application.log");
        createModuleLogger(CONTROLLER_LOGGER_NAME, logDir + "/controller.log");
    } catch (const std::exception& e) {
        qWarning() << "Failed to initialize logging:" << e.what(); // 初始化失败时保持 Qt 默认输出
        return;
    }

    qInstallMessageHandler(qtMessageHandler);
}

LogManager::~LogManager()
{
    qInstallMessageHandler(nullptr); // 卸载自定义日志处理器
    spdlog::shutdown();              // 立即写入所有日志并关闭日志系统
}

controller::LogCallback LogManager::controllerLogCallback() const
{
    return [](controller::LogLevel level, const std::string& fileName, int line, const std::string& message) {
        // 回调可能来自 controller 的工作线程；spdlog 的多线程 sink 线程安全，可直接写入
        logToModule(
            CONTROLLER_LOGGER_NAME,
            controllerLogLevelToSpdlogLevel(level),
            spdlog::source_loc{fileName.c_str(), line, ""},
            message
        );
    };
}

} // namespace view
