#pragma once
#include <memory>
#include <expected>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

namespace fh {
struct InputContextDeleter {
    void operator()(AVFormatContext* ctx) const;
};
using InputContextPtr = std::unique_ptr<AVFormatContext, InputContextDeleter>;
std::expected<InputContextPtr, int> makeInputContext(const char* filePath);

} // namespace fh