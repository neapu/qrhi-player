#include "Packet.h"

namespace fh {
void PacketDeleter::operator()(AVPacket* pkt) const
{
    if (pkt) {
        av_packet_free(&pkt);
    }
}

PacketPtr makePacket()
{
    AVPacket* pkt = av_packet_alloc();
    return PacketPtr(pkt);
}

} // namespace fh