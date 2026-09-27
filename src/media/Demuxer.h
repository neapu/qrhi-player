#pragma once
#include "InputContext.h"
#include "Packet.h"
#include "Logger.h"

namespace media {
class Demuxer {
public:
    struct Params {
        std::string url;
        LoggerPtr logger;
    };

    static std::unique_ptr<Demuxer> create(const Params& params);

    uint32_t streamCount() const;
    const AVStream* stream(uint32_t index) const;
    AVStream* stream(uint32_t index);

    bool seek(int64_t us);
    bool toHead();
    int64_t duration() const;

    std::expected<fh::PacketPtr, int> readPacket();

    AVFormatContext* formatContext() const;

private:
    Demuxer(const Params& params);
    bool initialize();

private:
    std::string m_url{};
    LoggerPtr m_logger{nullptr};
    fh::InputContextPtr m_inputContext{nullptr};
};
using DemuxerPtr = std::unique_ptr<Demuxer>;
} // namespace media