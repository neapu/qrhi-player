#pragma once
#include <memory>
#include "ffmpeg_helper/InputContext.h"
#include "Packet.h"
#include "Logger.h"

namespace controller {

class Demuxer {
public:
    static std::unique_ptr<Demuxer> create(const std::string& url, std::shared_ptr<Logger> logger);

    int streamCount() const;
    const AVStream* stream(int index) const;

    controller::PacketPtr readPacket(int serial);
    bool seek(int streamIndex, int64_t pts);

    double duration() const;
    
private:
    Demuxer() = default;
    bool initialize(const std::string& url, std::shared_ptr<Logger> logger);

private:
    std::shared_ptr<Logger> m_logger{nullptr};
    fh::InputContextPtr m_inputContext{nullptr};
};
using DemuxerPtr = std::unique_ptr<Demuxer>;

} // namespace controller