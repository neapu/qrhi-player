#include "BufferRef.h"

namespace fh {
void BufferRefDeleter::operator()(AVBufferRef* ctx) const
{
    if (ctx) {
        av_buffer_unref(&ctx);
    }
}
} // namespace fh
