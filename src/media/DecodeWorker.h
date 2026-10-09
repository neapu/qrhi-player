#pragma once
#include "Decoder.h"
#include "Logger.h"
#include "MediaPacket.h"

namespace media {
class DecodeWorker {
public:
    struct Params {
        LoggerPtr logger{};
        std::function<MediaPacket()> nextPacket{};
    };
    static std::unique_ptr<DecodeWorker> create(const Params& params, DecoderPtr&& decoder);

private:
    DecodeWorker(const Params& params);
    bool initialize();

private:
    LoggerPtr m_logger{nullptr};
    std::function<MediaPacket()> m_nextPacket{nullptr};
    DecoderPtr m_decoder{nullptr};

};

} // namespace media