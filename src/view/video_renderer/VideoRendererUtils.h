#pragma once

#include <QMatrix4x4>
#include <QSize>
#include "controller/Frame.h"

namespace view {

QMatrix4x4 createAspectRatioMatrix(const QSize& videoSize, const QSize& viewportSize);
QMatrix4x4 createYuvRangeMatrix(controller::IFrame::ColorRange colorRange);
QMatrix4x4 createYuvToRgbMatrix(controller::IFrame::ColorSpace colorSpace);

} // namespace view
