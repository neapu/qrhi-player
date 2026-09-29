#pragma once
#include <cstdint>

namespace media {
struct Statistics {
    struct {
        int64_t droppedPackets{0};
        int64_t decodedFrames{0};
        int64_t totalDecodeTimeUs{0};
        int64_t totalProcessTimeUs{0};
        int64_t totalQueueWaitTimeUs{0};
        int64_t latencyFrames{0};
        int64_t totalDecodeLatencyUs{0};
    } video;

};

};