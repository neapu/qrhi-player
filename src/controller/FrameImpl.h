#pragma once
#include "controller/Frame.h"
#include "ffmpeg_helper/Frame.h"

namespace controller {
class Frame final : public controller::IFrame {
public:
    static std::unique_ptr<Frame> create(int serial, FrameType type = FrameType::Normal);

    Frame(int serial, FrameType type = FrameType::Normal, fh::FramePtr&& frame = nullptr);

    bool isValid() const { return m_frame != nullptr; }

    FrameType type() const override { return m_type; }
    void setType(FrameType type) { m_type = type; }
    int serial() const override { return m_serial; }

    int width() const override;
    int height() const override;

    PixelFormat pixelFormat() const override;
    ColorSpace colorSpace() const override;
    ColorRange colorRange() const override;

    uint8_t* yData() const override;
    uint8_t* uData() const override;
    uint8_t* vData() const override;

    int yLineSize() const override;
    int uLineSize() const override;
    int vLineSize() const override;

    SampleFormat sampleFormat() const override;
    int sampleRate() const override;
    int channels() const override;
    int samples() const override;

    uint8_t* audioData() const override;
    int audioDataSize() const override;

    int64_t pts() const override;
    void setPts(int64_t pts);

    AVRational timebase() const;
    void setTimebase(AVRational timebase);

    void* rawFrame() override;
    const void* rawFrame() const override;
    void* release() override;

    AVFrame* avFrame();
    const AVFrame* avFrame() const;
private:
    fh::FramePtr m_frame{nullptr};
    FrameType m_type{FrameType::Normal};
    int m_serial{0};
};

} // controller namespace