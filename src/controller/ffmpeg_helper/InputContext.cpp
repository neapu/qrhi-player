#include "InputContext.h"
#include <stdexcept>

namespace fh {
void InputContextDeleter::operator()(AVFormatContext* ctx) const
{
    if (ctx) {
        avformat_close_input(&ctx);
    }
}

std::expected<InputContextPtr, int> makeInputContext(const char* filePath)
{
    AVFormatContext* ctx = nullptr;
    int ret = avformat_open_input(&ctx, filePath, nullptr, nullptr);
    if (ret < 0) {
        return std::unexpected(ret);
    }
    return InputContextPtr(ctx);
}

} // namespace fh