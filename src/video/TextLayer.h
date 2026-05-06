#pragma once

#include "video/Layer.h"

#include <QColor>
#include <QImage>
#include <QString>

namespace quewi::video {

// Text rendered into a QImage at a fixed pixel size. The image is
// rasterised once on construction and reused — text cues that animate
// (typewriter, fade) can render their own QImage and call rebuild().
class TextLayer : public Layer {
    Q_OBJECT
public:
    TextLayer(const QString &text, int fontPixelSize, const QColor &color,
              QObject *parent = nullptr);

    QImage currentFrame() const override { return m_frame; }

private:
    void rebuild();

    QString m_text;
    int     m_fontPixelSize = 96;
    QColor  m_color = Qt::white;
    QImage  m_frame;
};

} // namespace quewi::video
