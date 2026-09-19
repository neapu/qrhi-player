#include "DxvaDecoder.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace controller {
DxvaDecoder::DxvaDecoder(Type type)
    : Decoder(type)
{
}

DxvaDecoder::~DxvaDecoder()
{
}

bool DxvaDecoder::initialize(const Params& params)
{
    if (!params.logger)
        return false;
    m_logger = params.logger;
    if (!createContext(params.stream)) {
        return false;
    }
    if (!initializeHWContext(params.stream, static_cast<ID3D11Device*>(params.d3d11Device))) {
        return false;
    }
    return openCodec(params.stream);
}

bool DxvaDecoder::initializeHWContext(const AVStream* stream, ID3D11Device* d3d11Device)
{
    FUNC_TRACE();
    for (int i = 0; ; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_codecCtx->codec, i);
        if (!config) {
            LOGI("Enumerating HW configs finished, index: " << i);
            break;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            && config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
            m_hwPixelFormat = config->pix_fmt;
            break;
        }
    }
    if (m_hwPixelFormat == AV_PIX_FMT_NONE) {
        LOGE("Failed to find suitable HW pixel format");
        return false;
    }
    if (d3d11Device) {
        m_hwDeviceCtx = fh::BufferRefPtr(av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA));
        if (!m_hwDeviceCtx) {
            LOGE("Failed to allocate HW device context");
            return false;
        }
        auto* hwDevCtx = reinterpret_cast<AVHWDeviceContext*>(m_hwDeviceCtx->data);
        auto* d3d11DevCtx = static_cast<AVD3D11VADeviceContext*>(hwDevCtx->hwctx);
        d3d11DevCtx->device = d3d11Device;
        d3d11DevCtx->device->AddRef();
        int ret = av_hwdevice_ctx_init(m_hwDeviceCtx.get());
        if (ret < 0) {
            LOGE("Failed to initialize HW device context: " << fh::err2str(ret));
            return false;
        }
        LOGI("From user provided D3D11 device. HW device context initialized successfully.");
    } else {
        AVBufferRef* hwDeviceCtx = nullptr;
        int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        if (ret < 0) {
            LOGE("Failed to create HW device context: " << fh::err2str(ret));
            return false;
        }
        m_hwDeviceCtx = fh::BufferRefPtr(hwDeviceCtx);
        LOGI("From internally created D3D11 device. HW device context initialized successfully.");
    }

    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx.get());
    m_codecCtx->opaque = this;
    m_codecCtx->get_format = [](AVCodecContext* ctx, const AVPixelFormat* pix_fmts) -> AVPixelFormat {
        auto* decoder = reinterpret_cast<DxvaDecoder*>(ctx->opaque);
        for (const AVPixelFormat* p = pix_fmts; *p != -1; p++) {
            if (*p == decoder->m_hwPixelFormat) {
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    };

    return true;
}

} // namespace controller