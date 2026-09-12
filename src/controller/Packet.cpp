#include "Packet.h"

namespace controller {
std::unique_ptr<Packet> Packet::create(int serial, PacketType type)
{
    auto packet = fh::makePacket();
    if (!packet)
        return nullptr;
    return std::unique_ptr<Packet>(new Packet(serial, type, std::move(packet)));
}

Packet::Packet(int serial, PacketType type, fh::PacketPtr&& packet)
    : m_serial(serial), m_type(type), m_packet(std::move(packet))
{
}

Packet::PacketType Packet::type() const
{
    return m_type;
}

int Packet::serial() const
{
    return m_serial;
}

const AVPacket* Packet::avPacket() const
{
    return m_packet ? m_packet.get() : nullptr;
}

AVPacket* Packet::avPacket()
{
    return m_packet ? m_packet.get() : nullptr;
}


} // namespace controller