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

#define LOG_DEBUG(logger, ...) do { if (logger) logger->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::debug, __VA_ARGS__); } while(0)
#define LOG_INFO(logger, ...) do { if (logger) logger->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::info, __VA_ARGS__); } while(0)
#define LOG_WARN(logger, ...) do { if (logger) logger->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::warn, __VA_ARGS__); } while(0)
#define LOG_ERROR(logger, ...) do { if (logger) logger->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::err, __VA_ARGS__); } while(0)
#define LOG_CRITICAL(logger, ...) do { if (logger) logger->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::critical, __VA_ARGS__); } while(0)

#define FUNC_TRACE(logger, level) media::FunctionTracer functionTracer(logger, level, __FUNCTION__)