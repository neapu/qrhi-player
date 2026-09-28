#include "LogManager.h"
#include <QDir>
#include <QStandardPaths>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

extern "C" {
#include <libavutil/log.h>
}

namespace view {
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
        return spdlog::level::info;
    }
}

spdlog::level::level_enum avLogLevelToSpdlogLevel(int level)
{
    if (level <= AV_LOG_ERROR) {
        return spdlog::level::err;
    } else if (level <= AV_LOG_WARNING) {
        return spdlog::level::warn;
    } else if (level <= AV_LOG_INFO) {
        return spdlog::level::info;
    } else {
        return spdlog::level::debug;
    }
}

LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance;
}

QString LogManager::logDir()
{
#ifdef DEBUG
    const QString dir = QString::fromUtf8(SOURCE_DIR) + QStringLiteral("/logs");
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
#endif
    if (!QDir(dir).exists()) {
        if (!QDir().mkpath(dir)) {
            return {};
        }
    }
    return dir;
}

void LogManager::initialize()
{
    auto dir = logDir();
    if (dir.isEmpty()) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_logger = std::make_shared<spdlog::logger>("console", consoleSink);
#ifdef DEBUG
        m_logger->set_level(spdlog::level::debug);
#else
        m_logger->set_level(spdlog::level::info);
#endif
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        spdlog::set_default_logger(m_logger);
        return;
    }

#ifdef _WIN32
    spdlog::filename_t logFileT = QString("%1/main.log").arg(dir).toStdWString();
    spdlog::filename_t ffmpegLogFileT = QString("%1/ffmpeg.log").arg(dir).toStdWString();
#else
    spdlog::filename_t logFileT = QString("%1/main.log").arg(dir).toStdString();
    spdlog::filename_t ffmpegLogFileT = QString("%1/ffmpeg.log").arg(dir).toStdString();
#endif
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFileT, 1024 * 1024 * 5, 3);
    auto ffmpegFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(ffmpegLogFileT, 1024 * 1024 * 5, 3);

#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::info);
    m_logger = std::make_shared<spdlog::logger>("main", spdlog::sinks_init_list{fileSink, consoleSink});
    m_logger->set_level(spdlog::level::debug);
#else
    m_logger = std::make_shared<spdlog::logger>("main", fileSink);
    m_logger->set_level(spdlog::level::info);
#endif
    m_ffmpegLogger = std::make_shared<spdlog::logger>("ffmpeg", ffmpegFileSink);
#ifdef DEBUG
    m_ffmpegLogger->set_level(spdlog::level::debug);
#else
    m_ffmpegLogger->set_level(spdlog::level::info);
#endif
    m_ffmpegLogger->flush_on(spdlog::level::warn);
    m_ffmpegLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    m_logger->flush_on(spdlog::level::warn);
    m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    spdlog::set_default_logger(m_logger);

    qInstallMessageHandler(logQtMessageHandler);

    av_log_set_callback([](void* ptr, int level, const char* fmt, va_list vl) {
        auto logger = instance().m_ffmpegLogger;
        if (logger) {
            char buffer[1024];
            vsnprintf(buffer, sizeof(buffer), fmt, vl);
            logger->log(avLogLevelToSpdlogLevel(level), buffer);
        }
    });
}

void LogManager::shutdown()
{
    if (m_logger) {
        m_logger->flush();
    }
    if (m_ffmpegLogger) {
        m_ffmpegLogger->flush();
    }
    qInstallMessageHandler(nullptr);
    spdlog::shutdown();
}

void LogManager::logQtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    
    auto logger = instance().m_logger;
    if (!logger) {
        return;
    }

    spdlog::source_loc loc{
        context.file,
        context.line,
        ""
    };
    logger->log(loc, qtMsgTypeToSpdlogLevel(type), msg.toStdString());
}

} // namespace view