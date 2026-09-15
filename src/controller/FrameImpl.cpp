#include "FrameImpl.h"

namespace {
controller::Frame::PixelFormat toPixelFormat(AVPixelFormat format)
{
    using enum controller::Frame::PixelFormat;
    switch (format) {
        case AV_PIX_FMT_YUV420P: return YUV420P;
        default: return None;
    }
}
AVPixelFormat toAVPixelFormat(controller::Frame::PixelFormat format)
{
    using enum controller::Frame::PixelFormat;
    switch (format) {
        case YUV420P: return AV_PIX_FMT_YUV420P;
        default: return AV_PIX_FMT_NONE;
    }
}

controller::Frame::ColorSpace toColorSpace(AVColorSpace space)
{
    using enum controller::Frame::ColorSpace;
    switch (space) {
        case AVCOL_SPC_BT709: return BT709;
        default: return BT601;
    }
}
AVColorSpace toAVColorSpace(controller::Frame::ColorSpace space)
{
    using enum controller::Frame::ColorSpace;
    switch (space) {
        case BT709: return AVCOL_SPC_BT709;
        default: return AVCOL_SPC_BT470BG;
    }
}

controller::Frame::ColorRange toColorRange(AVColorRange range)
{
    using enum controller::Frame::ColorRange;
    switch (range) {
        case AVCOL_RANGE_MPEG: return Limited;
        default: return Full;
    }
}
AVColorRange toAVColorRange(controller::Frame::ColorRange range)
{
    using enum controller::Frame::ColorRange;
    switch (range) {
        case Limited: return AVCOL_RANGE_MPEG;
        default: return AVCOL_RANGE_JPEG;
    }
}

controller::Frame::SampleFormat toSampleFormat(AVSampleFormat format)
{
    using enum controller::Frame::SampleFormat;
    switch (format) {
        case AV_SAMPLE_FMT_S16: return S16LE;
        default: return None;
    }
}
AVSampleFormat toAVSampleFormat(controller::Frame::SampleFormat format)
{
    using enum controller::Frame::SampleFormat;
    switch (format) {
        case S16LE: return AV_SAMPLE_FMT_S16;
        default: return AV_SAMPLE_FMT_NONE;
    }
}
}

namespace controller {
std::unique_ptr<Frame> Frame::create(int serial, FrameType type)
{
    auto framePtr = fh::makeFrame();
    if (!framePtr)
        return nullptr;
    return std::make_unique<Frame>(serial, type, std::move(framePtr));
}

Frame::Frame(int serial, FrameType type, fh::FramePtr&& frame)
    : m_serial(serial), m_type(type), m_frame(std::move(frame))
{
}

int Frame::width() const
{
    return m_frame ? m_frame->width : 0;
}

int Frame::height() const
{
    return m_frame ? m_frame->height : 0;
}

Frame::PixelFormat Frame::pixelFormat() const
{
    return m_frame ? toPixelFormat(static_cast<AVPixelFormat>(m_frame->format)) : PixelFormat::None;
}

Frame::ColorSpace Frame::colorSpace() const
{
    return m_frame ? toColorSpace(m_frame->colorspace) : ColorSpace::BT601;
}

Frame::ColorRange Frame::colorRange() const
{
    return m_frame ? toColorRange(m_frame->color_range) : ColorRange::Full;
}

uint8_t* Frame::yData() const
{
    return m_frame ? m_frame->data[0] : nullptr;
}

uint8_t* Frame::uData() const
{
    return m_frame ? m_frame->data[1] : nullptr;
}

uint8_t* Frame::vData() const
{
    return m_frame ? m_frame->data[2] : nullptr;
}

int Frame::yLineSize() const
{
    return m_frame ? m_frame->linesize[0] : 0;
}

int Frame::uLineSize() const
{
    return m_frame ? m_frame->linesize[1] : 0;
}

int Frame::vLineSize() const
{
    return m_frame ? m_frame->linesize[2] : 0;
}

Frame::SampleFormat Frame::sampleFormat() const
{
    return m_frame ? toSampleFormat(static_cast<AVSampleFormat>(m_frame->format)) : SampleFormat::None;
}

int Frame::sampleRate() const
{
    return m_frame ? m_frame->sample_rate : 0;
}

int Frame::channels() const
{
    return m_frame ? m_frame->ch_layout.nb_channels : 0;
}

int Frame::samples() const
{
    return m_frame ? m_frame->nb_samples : 0;
}

uint8_t* Frame::audioData() const
{
    return m_frame ? m_frame->data[0] : nullptr;
}

int Frame::audioDataSize() const
{
    if (!m_frame) {
        return 0;
    }
    const int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(m_frame->format));
    return bytesPerSample > 0 ? m_frame->nb_samples * m_frame->ch_layout.nb_channels * bytesPerSample : 0;
}

int64_t Frame::pts() const
{
    return m_frame ? m_frame->pts : 0;
}

void Frame::setPts(int64_t pts)
{
    if (m_frame) {
        m_frame->pts = pts;
    }
}

AVRational Frame::timebase() const
{
    return m_frame ? m_frame->time_base : AVRational{0, 1};
}

void Frame::setTimebase(AVRational timebase)
{
    if (m_frame) {
        m_frame->time_base = timebase;
    }
}

void* Frame::rawFrame()
{
    return m_frame ? m_frame.get() : nullptr;
}

const void* Frame::rawFrame() const
{
    return m_frame ? m_frame.get() : nullptr;
}

void* Frame::release()
{
    return m_frame ? m_frame.release() : nullptr;
}

AVFrame* Frame::avFrame()
{
    return m_frame ? m_frame.get() : nullptr;
}

const AVFrame* Frame::avFrame() const
{
    return m_frame ? m_frame.get() : nullptr;
}
} // controller namespace