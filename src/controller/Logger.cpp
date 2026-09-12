#include "Logger.h"

namespace controller {
LogWorker::LogWorker(LogLevel level, const LogCallback& logCallback, const std::source_location& location)
    : m_level(level), m_logCallback(logCallback), m_location(location)
{
}

LogWorker::~LogWorker()
{
    if (m_logCallback) {
        m_logCallback(m_level, m_location.file_name(), m_location.line(), m_data.str());
    }
}

FunctionTracer::FunctionTracer(const LogCallback& logCallback, const std::source_location& location) noexcept
    : m_logCallback(logCallback), m_location(location)
{
    if (m_logCallback) {
        std::string message = "Entering function: ";
        message += m_location.function_name();
        m_logCallback(LogLevel::Debug, m_location.file_name(), m_location.line(), message);
    }
}

FunctionTracer::~FunctionTracer()
{
    if (m_logCallback) {
        std::string message = "Exiting function: ";
        message += m_location.function_name();
        m_logCallback(LogLevel::Debug, m_location.file_name(), m_location.line(), message);
    }
}

Logger::Logger(const LogCallback& logCallback)
    : m_logCallback(logCallback)
{
}

LogWorker Logger::debug(const std::source_location& location)
{
    return LogWorker(LogLevel::Debug, m_logCallback, location);
}

LogWorker Logger::info(const std::source_location& location)
{
    return LogWorker(LogLevel::Info, m_logCallback, location);
}

LogWorker Logger::warning(const std::source_location& location)
{
    return LogWorker(LogLevel::Warning, m_logCallback, location);
}

LogWorker Logger::error(const std::source_location& location)
{
    return LogWorker(LogLevel::Error, m_logCallback, location);
}

LogWorker Logger::fatal(const std::source_location& location)
{
    return LogWorker(LogLevel::Fatal, m_logCallback, location);
}

std::unique_ptr<FunctionTracer> Logger::trace(const std::source_location& location)
{
    return std::make_unique<FunctionTracer>(m_logCallback, location);
}

} // namespace controller