#pragma once
#include <variant>
#include "Frame.h"

namespace media {
struct EndFrame {
    int serial{0};
};
struct EmptyFrame {};
using MediaFrame = std::variant<fh::FramePtr, EndFrame, EmptyFrame>;
}