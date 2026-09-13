#pragma once

#include <string>
#include <source_location>
#include <sstream>
#include "controller/Controller.h"

namespace controller {
class LogWorker {
public:
    LogWorker(LogLevel level, const LogCallback& logCallback, const std::source_location& location = std::source_location::current());
    ~LogWorker();

    template <typename T>
    LogWorker& operator<<(T&& t)
    {
        m_data << std::forward<T>(t);
        return *this;
    }

private:
    LogLevel m_level;
    LogCallback m_logCallback;
    std::source_location m_location;
    std::stringstream m_data;
};

class FunctionTracer {
public:
    FunctionTracer(const LogCallback& logCallback, const std::source_location& location = std::source_location::current()) noexcept;
    ~FunctionTracer();

private:
    LogCallback m_logCallback;
    std::source_location m_location;
};

class Logger {
public:
    Logger(const LogCallback& logCallback);

    LogWorker debug(const std::source_location& location = std::source_location::current());
    LogWorker info(const std::source_location& location = std::source_location::current());
    LogWorker warning(const std::source_location& location = std::source_location::current());
    LogWorker error(const std::source_location& location = std::source_location::current());
    LogWorker fatal(const std::source_location& location = std::source_location::current());

    std::unique_ptr<FunctionTracer> trace(const std::source_location& location = std::source_location::current());

private:
    LogCallback m_logCallback;
};
} // namespace controller

#define LOGE(x) \
    if (m_logger) { \
        m_logger->error() << x; \
    }

#define LOGW(x) \
    if (m_logger) { \
        m_logger->warning() << x; \
    }

#define LOGI(x) \
    if (m_logger) { \
        m_logger->info() << x; \
    }

#define LOGD(x) \
    if (m_logger) { \
        m_logger->debug() << x; \
    }

#define LOGF(x) \
    if (m_logger) { \
        m_logger->fatal() << x; \
    }

#define FUNC_TRACE() \
    std::unique_ptr<controller::FunctionTracer> funcTracer = m_logger ? m_logger->trace() : nullptr;