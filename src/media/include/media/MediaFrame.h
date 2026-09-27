#pragma once
#include <variant>
#include "Frame.h"

namespace media {
struct EndFrame {};
struct EmptyFrame {};
using MediaFrame = std::variant<fh::FramePtr, EndFrame, EmptyFrame>;
}