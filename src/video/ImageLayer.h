#pragma once

#include "video/Layer.h"

#include <QImage>
#include <QString>

namespace quewi::video {

// Static image — loaded once at construction, replays the same frame
// every paint. Cheap; many image cues can layer simultaneously without
// any decode cost.
class ImageLayer : public Layer {
    Q_OBJECT
public:
    explicit ImageLayer(const QString &filePath, QObject *parent = nullptr);

    QImage currentFrame() const override { return m_frame; }

private:
    QImage m_frame;
};

} // namespace quewi::video
