#pragma once
#include <cstdint>

namespace media {
struct Statistics {
    struct {
        int64_t decodedFrames{0};
    } video;
};

};