#include "video/ImageLayer.h"

namespace quewi::video {

ImageLayer::ImageLayer(const QString &filePath, QObject *parent)
    : Layer(parent), m_frame(filePath)
{
    if (m_frame.isNull()) {
        // QImage(filePath) returns null on missing/unsupported files —
        // the compositor skips null frames so the layer is benign.
    }
    emit frameAvailable();
}

} // namespace quewi::video
