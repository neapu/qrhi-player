#include "VideoRendererUtils.h"

namespace view {

QMatrix4x4 createAspectRatioMatrix(const QSize& videoSize, const QSize& viewportSize)
{
    QMatrix4x4 matrix;
    if (videoSize.width() <= 0 || videoSize.height() <= 0
        || viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return matrix;
    }

    const float videoAspect = static_cast<float>(videoSize.width()) / videoSize.height();
    const float viewportAspect = static_cast<float>(viewportSize.width()) / viewportSize.height();
    if (viewportAspect > videoAspect) {
        matrix.scale(videoAspect / viewportAspect, 1.0f);
    } else {
        matrix.scale(1.0f, viewportAspect / videoAspect);
    }
    return matrix;
}

QMatrix4x4 createYuvRangeMatrix(controller::IFrame::ColorRange colorRange)
{
    static const QMatrix4x4 limitedRange{
        255.0f / 219.0f, 0.0f,          0.0f,           -16.0f / 219.0f,
        0.0f,           255.0f / 224.0f, 0.0f,          -128.0f / 224.0f,
        0.0f,           0.0f,           255.0f / 224.0f, -128.0f / 224.0f,
        0.0f,           0.0f,           0.0f,            1.0f
    };
    static const QMatrix4x4 fullRange{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 1.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return colorRange == controller::IFrame::ColorRange::Full ? fullRange : limitedRange;
}

QMatrix4x4 createYuvToRgbMatrix(controller::IFrame::ColorSpace colorSpace)
{
    static const QMatrix4x4 bt601{
        1.0f,  0.0f,       1.402f,    0.0f,
        1.0f, -0.344136f, -0.714136f, 0.0f,
        1.0f,  1.772f,     0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };
    static const QMatrix4x4 bt709{
        1.0f,  0.0f,       1.5748f,   0.0f,
        1.0f, -0.187324f, -0.468124f, 0.0f,
        1.0f,  1.8556f,    0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };

    return colorSpace == controller::IFrame::ColorSpace::BT709 ? bt709 : bt601;
}

} // namespace view
