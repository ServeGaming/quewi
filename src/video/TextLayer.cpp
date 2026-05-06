#include "video/TextLayer.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QTextOption>

namespace quewi::video {

TextLayer::TextLayer(const QString &text, int fontPixelSize,
                     const QColor &color, QObject *parent)
    : Layer(parent)
    , m_text(text)
    , m_fontPixelSize(std::max(8, fontPixelSize))
    , m_color(color.isValid() ? color : Qt::white)
{
    rebuild();
    emit frameAvailable();
}

void TextLayer::rebuild()
{
    QFont f;
    f.setPixelSize(m_fontPixelSize);
    f.setWeight(QFont::DemiBold);
    QFontMetrics fm(f);

    // Wrap to a wide canvas — 1920 px is a generous default; the
    // compositor scales the rendered image to the layer's geometry
    // anyway, so the absolute size matters less than the aspect.
    constexpr int kCanvasW = 1920;
    constexpr int kSidePad = 24;
    const QRect available(kSidePad, kSidePad,
                          kCanvasW - 2 * kSidePad, INT_MAX / 2);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    opt.setAlignment(Qt::AlignCenter);
    const QRect bound = fm.boundingRect(available, Qt::AlignCenter | Qt::TextWordWrap, m_text);
    const int h = std::max(fm.height() + 2 * kSidePad, bound.height() + 2 * kSidePad);

    QImage img(kCanvasW, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
    p.setFont(f);
    p.setPen(m_color);
    p.drawText(QRect(kSidePad, kSidePad, kCanvasW - 2 * kSidePad, h - 2 * kSidePad),
               Qt::AlignCenter | Qt::TextWordWrap, m_text);
    m_frame = std::move(img);
}

} // namespace quewi::video
