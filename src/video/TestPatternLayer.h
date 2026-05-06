#pragma once

#include "video/Layer.h"

#include <QImage>

namespace quewi::video {

// Calibration test pattern for projection mapping. Renders once into
// a 1920×1080 QImage:
//   - 10×10 grid so the user can see the warp curvature
//   - Corner markers (TL, TR, BR, BL) so it's obvious which corner is
//     where after the warp scrambles the layout
//   - Centre crosshair for fine alignment
//   - Centre circle that becomes visibly oblong if the projector is
//     off-axis (a fast sanity check — circles don't lie)
//   - 16:9 safe-zone outline at 90% so the user can confirm the
//     projector is actually filling the screen
//
// The layer is created+removed by ProjectionMappingDialog while the
// dialog is open. Z-order is intentionally high so it floats over any
// running cue's content during setup.
class TestPatternLayer : public Layer {
    Q_OBJECT
public:
    explicit TestPatternLayer(QObject *parent = nullptr);

    QImage currentFrame() const override { return m_frame; }

private:
    void rebuild();
    QImage m_frame;
};

} // namespace quewi::video
