#include "ControllerImpl.h"
#include <print>
#include <chrono>

namespace controller {
std::unique_ptr<IController> IController::create(const Params& params)
{
    auto controller = std::make_unique<Controller>(params);
    if (controller->initialize()) {
        return controller;
    }
    return nullptr;
}

Controller::Controller(const Params& params) : m_params(params)
{
}

Controller::~Controller()
{
    if (m_logger) {
        auto tracer = m_logger->trace();
    }
}

bool Controller::initialize()
{
    if (m_params.logCallback) {
        m_logger = std::make_unique<Logger>(m_params.logCallback);
    } else {
        m_logger = std::make_unique<Logger>([](LogLevel level, const std::string& fileName, int line, const std::string& message) {
            std::string logLevel{"Debug"};
            switch (level) {
                case LogLevel::Debug:
                    logLevel = "Debug";
                    break;
                case LogLevel::Info:
                    logLevel = "Info";
                    break;
                case LogLevel::Warning:
                    logLevel = "Warning";
                    break;
                case LogLevel::Error:
                    logLevel = "Error";
                    break;
                case LogLevel::Fatal:
                    logLevel = "Fatal";
                    break;
            };
            const auto now = std::chrono::system_clock::now();
            const auto timet = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &timet); // std::localtime 返回静态缓冲区，多线程调用是数据竞争
#else
            localtime_r(&timet, &tm);
#endif
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            std::print("[{}-{:02}-{:02} {:02}:{:02}:{:02}.{}] [{}] {}:{} {}\n",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec,
                       static_cast<int>(ms),
                       logLevel, fileName, line, message);
        });
    }
    
    auto tracer = m_logger->trace();

    m_logger->info() << "Opening file: " << m_params.url;

    return true;
}

FramePtr Controller::nextVideoFrame() const
{
    return nullptr;
}

FramePtr Controller::nextAudioFrame() const
{
    return nullptr;
}

double Controller::duration() const
{
    return 0.0;
}

void Controller::seek(double timepoint)
{
}

void Controller::pauseOrResume()
{
}

bool Controller::isPaused() const
{
    return false;
}

} // namespace controller