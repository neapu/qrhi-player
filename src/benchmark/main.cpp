#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <format>
#include <mutex>
#include <print>
#include <string>

#include "AudioPlayer.h"
#include "Stats.h"
#include "controller/Controller.h"
#include "controller/Frame.h"

namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_logMutex;

// controller工作线程调用，必须线程安全；Debug级静默避免刷屏
void logCallback(controller::LogLevel level, const std::string& file, int line, const std::string& msg)
{
    using controller::LogLevel;
    if (level == LogLevel::Debug) {
        return;
    }
    static const char* tags[] = { "Debug", "Info", "Warning", "Error", "Fatal" };
    std::lock_guard lock(g_logMutex);
    std::println("[controller][{}] {} ({}:{})", tags[static_cast<int>(level)], msg, file, line);
}

std::string formatClock(double sec)
{
    if (sec < 0) {
        sec = 0;
    }
    const int total = static_cast<int>(sec);
    return std::format("{:02d}:{:02d}", total / 60, total % 60);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::println("用法: {} <媒体文件>", argv[0]);
        std::println("交互: 空格=暂停/恢复  左右方向键=±10s  上下方向键=±60s  鼠标点击=按位置比例seek  Esc=退出");
        return 1;
    }
    const std::string mediaUrl = argv[1];

    // 已用SDL_MAIN_HANDLED禁用SDL_main入口，需手动就绪
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::println("SDL_Init失败: {}", SDL_GetError());
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        // 音频子系统失败不致命：AudioPlayer::start会失败，demo退化为只排空音频队列
        std::println("警告: SDL音频子系统初始化失败({}), 将无声音播放", SDL_GetError());
    }

    // 窗口/渲染器先于controller创建，避免起播阶段时钟空转导致跳帧
    SDL_Window* window = SDL_CreateWindow("controller_benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::println("创建窗口失败: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::println("创建渲染器失败: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    controller::IController::Params params;
    params.url = mediaUrl;
    params.logCallback = logCallback;
    // create()内部起demux/解码线程并锚定时钟，失败返回nullptr（如URL无效）
    auto controller = controller::IController::create(params);
    if (!controller) {
        std::println("打开媒体失败: {}", mediaUrl);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "controller_benchmark",
            std::format("打开媒体失败:\n{}", mediaUrl).c_str(), window);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    const double durationSec = controller->duration();

    bench::Stats stats;
    bench::AudioPlayer audio(*controller, stats);

    // ---- 音频设备：controller初始化完即可取输出参数打开，无需等首个音频帧 ----
    // 无音频流或设备打开失败时置drainOnly：音频帧无人消费会填满队列阻塞demux线程
    bool audioDrainOnly = false;
    if (auto params = controller->audioParams()) {
        if (audio.start(params->sampleRate, params->channels)) {
            stats.onAudioStarted(params->sampleRate, params->channels);
            std::println("音频设备已开启: {}Hz x {}声道 (S16LE)", params->sampleRate, params->channels);
        } else {
            std::println("警告: 打开SDL音频设备失败({}), 转为只排空音频队列", SDL_GetError());
            audioDrainOnly = true;
        }
    } else {
        audioDrainOnly = true; // 无音频流
    }

    SDL_Texture* texture = nullptr; // YUV420P -> SDL_PIXELFORMAT_IYUV
    int texW = 0;
    int texH = 0;
    int maxSerialSeen = -1;
    bool videoEnded = false;
    int64_t lastShownPtsUs = 0;
    double lastSeekTargetSec = 0.0;
    bool awaitSeekFrame = false; // seek已发出、尚未等到新serial首帧

    const auto seekTo = [&](double target) {
        if (durationSec > 0) {
            target = std::clamp(target, 0.0, durationSec);
        } else if (target < 0) {
            target = 0;
        }
        stats.onSeekRequested(target, maxSerialSeen, Clock::now());
        lastSeekTargetSec = target;
        awaitSeekFrame = true;
        controller->seek(target);
    };
    // seek等待期以seek目标为当前位置，其余时间以最近显示帧pts为准
    const auto positionSec = [&]() {
        return awaitSeekFrame ? lastSeekTargetSec : lastShownPtsUs / 1e6;
    };

    bool running = true;
    auto lastTitleAt = Clock::now();
    auto lastConsoleAt = lastTitleAt;
    std::string lastConsoleLine; // EOF停泊期状态不再变化，避免重复刷屏
    constexpr auto TITLE_INTERVAL = std::chrono::milliseconds(500);
    constexpr auto CONSOLE_INTERVAL = std::chrono::seconds(5);

    std::println("开始播放: {} (时长 {}s)", mediaUrl, durationSec);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    controller->pauseOrResume();
                    break;
                case SDLK_LEFT:
                    seekTo(positionSec() - 10.0);
                    break;
                case SDLK_RIGHT:
                    seekTo(positionSec() + 10.0);
                    break;
                case SDLK_DOWN:
                    seekTo(positionSec() - 60.0);
                    break;
                case SDLK_UP:
                    seekTo(positionSec() + 60.0);
                    break;
                default:
                    break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT && durationSec > 0) {
                    int w = 0;
                    int h = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    if (w > 0) {
                        seekTo(durationSec * event.button.x / w);
                    }
                }
                break;
            default:
                break;
            }
        }

        // ---- 音频排空：未开设备时音频帧无人消费会填满队列阻塞demux线程，进而拖垮视频 ----
        if (audioDrainOnly) {
            for (int i = 0; i < 16; ++i) {
                if (!controller->nextAudioFrame()) {
                    break;
                }
            }
        }

        // ---- 视频拉取：常规节奏每圈取一帧，seek后的过期serial帧就地排干 ----
        for (int guard = 0; guard < 64; ++guard) {
            controller::FramePtr frame = controller->nextVideoFrame();
            if (!frame) {
                // nullptr含义有歧义（暂停/时钟未到/队列空/EOF后），只统计有效播放期
                if (!videoEnded && !controller->isPaused()) {
                    stats.onNullPull();
                }
                break;
            }
            if (frame->type() == controller::IFrame::FrameType::End) {
                videoEnded = true;
                stats.onVideoEnd();
                break;
            }
            if (frame->serial() < maxSerialSeen) {
                stats.onStaleFrameDiscarded();
                continue;
            }
            maxSerialSeen = frame->serial();
            videoEnded = false;
            awaitSeekFrame = false;

            if (!texture || frame->width() != texW || frame->height() != texH) {
                if (texture) {
                    SDL_DestroyTexture(texture);
                }
                texW = frame->width();
                texH = frame->height();
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                    SDL_TEXTUREACCESS_STREAMING, texW, texH);
                if (!texture) {
                    std::println("创建纹理失败: {}", SDL_GetError());
                    running = false;
                    break;
                }
                // 窗口尺寸不变，按视频宽高比做信箱式缩放
                SDL_RenderSetLogicalSize(renderer, texW, texH);
                std::println("视频: {}x{} YUV420P", texW, texH);
            }
            // IFrame的Y/U/V平面与SDL的IYUV纹理一一对应
            SDL_UpdateYUVTexture(texture, nullptr,
                frame->yData(), frame->yLineSize(),
                frame->uData(), frame->uLineSize(),
                frame->vData(), frame->vLineSize());
            lastShownPtsUs = frame->pts();
            stats.onVideoFrame(frame->pts(), frame->serial(), Clock::now());
            break;
        }

        // ---- 渲染：vsync驱动整个循环的节奏 ----
        SDL_RenderClear(renderer);
        if (texture) {
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        }
        SDL_RenderPresent(renderer);

        const auto now = Clock::now();
        const bool paused = controller->isPaused();
        stats.onLoop(now, paused);

        if (now - lastTitleAt >= TITLE_INTERVAL) {
            lastTitleAt = now;
            const std::string line = stats.liveLine(durationSec, paused, videoEnded);
            SDL_SetWindowTitle(window, std::format("controller_benchmark | {}", line).c_str());
        }
        if (now - lastConsoleAt >= CONSOLE_INTERVAL) {
            lastConsoleAt = now;
            std::string line = stats.liveLine(durationSec, paused, videoEnded);
            if (line != lastConsoleLine) {
                lastConsoleLine = std::move(line);
                std::println("[{}/{}] {}", formatClock(lastShownPtsUs / 1e6), formatClock(durationSec),
                    lastConsoleLine);
            }
        }
    }

    // ---- 退出：先停音频回调，再销毁controller（验证线程join无死锁），最后SDL ----
    audio.stop();
    const auto destroyStart = Clock::now();
    controller.reset();
    const auto destroyMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - destroyStart).count();
    std::println("controller析构耗时: {}ms", destroyMs);

    stats.printSummary(durationSec);

    if (texture) {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
