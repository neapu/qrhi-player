#pragma once
#include <cstdint>

namespace controller {

struct StatisticsData {
    struct Video {
        uint64_t droppedFrames{0};
        uint64_t decodedFrames{0};
    } video;
};

} // namespace controller