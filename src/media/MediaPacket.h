#pragma once
#include <variant>
#include "Packet.h"

namespace media {
struct EndPacket {
    int serial{0};
};
struct FlashPacket {
    int serial{0};
};
struct EmptyPacket {};
using MediaPacket = std::variant<fh::PacketPtr, EndPacket, FlashPacket, EmptyPacket>;
} // namespace media