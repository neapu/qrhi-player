#pragma once
#include "ffmpeg_helper/Packet.h"

namespace controller {
class Packet {
public:
    enum class PacketType {
        Normal,
        End
    };

    static std::unique_ptr<Packet> create(int serial, PacketType type);

    PacketType type() const;
    int serial() const;

    const AVPacket* avPacket() const;
    AVPacket* avPacket();

private:
    Packet(int serial, PacketType type, fh::PacketPtr&& packet);

private:
    PacketType m_type;
    int m_serial;
    fh::PacketPtr m_packet;
};
using PacketPtr = std::unique_ptr<Packet>;

} // namespace controller