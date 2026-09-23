#include "Logger.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#ifdef _WIN32
// 必须包含 <windows.h>，不能只包含 <stringapiset.h>：
// <stringapiset.h> 会间接引入 <minwindef.h> -> <winnt.h>，而 <winnt.h> 依赖
// <windows.h> 才会定义的架构宏（_AMD64_ / _X86_ / _ARM64_ 等），
// 缺失时会在 winnt.h(169) 报 "#error: No Target Architecture"。
// 注意 spdlog 从 1.x 起不再在 os.h 中引入 windows.h，因此这里没有其他头文件能补上该宏。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
#ifdef _WIN32
std::wstring utf8ToWide(const std::string& str)
{
    if (str.empty()) {
        return std::wstring{};
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
spdlog::filename_t toFilenameT(const std::string& str)
{
#ifdef SPDLOG_WCHAR_FILENAMES
    return utf8ToWide(str);
#else
    return str;
#endif
}
#else
spdlog::filename_t toFilenameT(const std::string& str)
{
    return str;
}
#endif // _WIN32

}

namespace media {
LoggerPtr createLogger(const std::string& loggerName, const std::string& logDir)
{
    if (logDir.empty()) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>(loggerName, spdlog::sinks_init_list{consoleSink});
#ifdef DEBUG
        logger->set_level(spdlog::level::debug);
#else
        logger->set_level(spdlog::level::info);
#endif
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        spdlog::register_logger(logger);
        return logger;
    }

    // debug 模式下同时输出到文件和控制台，release 模式下只输出到文件
    auto fileName = toFilenameT(std::format("{}/media_{}.log", logDir, loggerName));
    
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fileName, 1024 * 1024 * 5, 3);
#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(loggerName, spdlog::sinks_init_list{fileSink, consoleSink});
    logger->set_level(spdlog::level::debug);
#else
    auto logger = std::make_shared<spdlog::logger>(loggerName, spdlog::sinks_init_list{fileSink});
    logger->set_level(spdlog::level::info);
#endif
    logger->flush_on(spdlog::level::warn);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    spdlog::register_logger(logger);
    return logger;

}

FunctionTracer::FunctionTracer(std::shared_ptr<spdlog::logger> logger, spdlog::level::level_enum level, const std::string& functionName)
    : m_logger(std::move(logger)), m_level(level), m_functionName(functionName) 
{
    if (m_logger) {
        m_logger->log(m_level, "Entering function {}", m_functionName);
    }
}

FunctionTracer::~FunctionTracer()
{
    if (m_logger) {
        m_logger->log(m_level, "Exiting function {}", m_functionName);
    }
}

} // namespace media