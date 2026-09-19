#pragma once
#include "Decoder.h"
#include <libavutil/hwcontext_d3d11va.h>
#include "ffmpeg_helper/BufferRef.h"

namespace controller {
class DxvaDecoder : public Decoder {
public:
    explicit DxvaDecoder(Type type);
    ~DxvaDecoder() override;
protected:
    bool initialize(const Params& params) override;
    bool initializeHWContext(const AVStream* stream, ID3D11Device* d3d11Device);

protected:
    fh::BufferRefPtr m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixelFormat{AV_PIX_FMT_NONE};
};

} // namespace controller