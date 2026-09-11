#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#ifdef DEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace {
spdlog::level::level_enum qtLogLevelToSpdlogLevel(QtMsgType type) {
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

void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    try {
        spdlog::default_logger()->log(
            spdlog::source_loc{
                context.file ? context.file : "",
                context.line,
                context.function ? context.function : ""
            },
            qtLogLevelToSpdlogLevel(type),
            msg.toStdString()
        );
    } catch (...) {
        // 日志处理函数不能抛异常，否则可能导致程序崩溃
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

void initializeLogging(const QString& logDir)
{
    if (!QDir(logDir).exists()) {
        if (!QDir().mkpath(logDir)) {
            qWarning() << "Failed to create log directory:" << logDir;
            return;
        }
    }

    const auto logFilePath = logDir + "/application.log";

    // 单个文件最大10MB，最多保留5个文件
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFilePath.toStdString(),
        10 * 1024 * 1024, // 10MB
        5                 // 保留5个文件
    );

#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("qt", spdlog::sinks_init_list{fileSink, consoleSink});
#else
    auto logger = std::make_shared<spdlog::logger>("qt", fileSink);
#endif

    logger->set_level(spdlog::level::debug); // 设置日志级别
    logger->flush_on(spdlog::level::warn);   // 设置立即刷新日志
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v"); // 设置日志格式
    
    spdlog::set_default_logger(std::move(logger));
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const auto logDir = resolveLogDir();
    initializeLogging(logDir);
    qInstallMessageHandler(qtMessageHandler);

    const auto exitCode = app.exec();

    qInstallMessageHandler(nullptr); // 卸载自定义日志处理器
    spdlog::shutdown(); // 立即写入所有日志并关闭日志系统

    return exitCode;
}