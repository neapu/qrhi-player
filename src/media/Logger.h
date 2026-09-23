#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace media {
using LoggerPtr = std::shared_ptr<spdlog::logger>;
LoggerPtr createLogger(const std::string& loggerName, const std::string& logDir);
class FunctionTracer {
public:
    FunctionTracer(LoggerPtr logger, spdlog::level::level_enum level, const std::string& functionName);
    ~FunctionTracer();

private:
    LoggerPtr m_logger;
    spdlog::level::level_enum m_level;
    std::string m_functionName;
};

} // namespace media

#define LOG_DEBUG(logger, ...) do { if (logger) logger->debug(__VA_ARGS__); } while(0)
#define LOG_INFO(logger, ...) do { if (logger) logger->info(__VA_ARGS__); } while(0)
#define LOG_WARN(logger, ...) do { if (logger) logger->warn(__VA_ARGS__); } while(0)
#define LOG_ERROR(logger, ...) do { if (logger) logger->error(__VA_ARGS__); } while(0)
#define LOG_CRITICAL(logger, ...) do { if (logger) logger->critical(__VA_ARGS__); } while(0)
#define FUNC_TRACE(logger, level) media::FunctionTracer functionTracer(logger, level, __FUNCTION__)