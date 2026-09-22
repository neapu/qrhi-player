#pragma once
#include <memory>

extern "C" {
#include <libavcodec/packet.h>
}

namespace fh {
struct PacketDeleter {
    void operator()(AVPacket* pkt) const;
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
PacketPtr makePacket();

} // namespace fh