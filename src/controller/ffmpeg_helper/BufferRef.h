#pragma once
#include <memory>
extern "C" {
#include <libavutil/buffer.h>
}

namespace fh {
struct BufferRefDeleter {
    void operator()(AVBufferRef* ctx) const;
};
using BufferRefPtr = std::unique_ptr<AVBufferRef, BufferRefDeleter>;

} // namespace fh