#include "video/TestPatternLayer.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace quewi::video {

TestPatternLayer::TestPatternLayer(QObject *parent) : Layer(parent)
{
    rebuild();
    setZOrder(10000);            // float above any running cue
    emit frameAvailable();
}

void TestPatternLayer::rebuild()
{
    constexpr int W = 1920;
    constexpr int H = 1080;

    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0x10, 0x10, 0x14));      // near-black background

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── Grid: 10 columns × 10 rows ──────────────────────────────────
    p.setPen(QPen(QColor(0x55, 0x55, 0x60), 1));
    for (int i = 1; i < 10; ++i) {
        const int x = (W * i) / 10;
        const int y = (H * i) / 10;
        p.drawLine(x, 0, x, H);
        p.drawLine(0, y, W, y);
    }

    // Heavier centre lines.
    p.setPen(QPen(QColor(0x90, 0x90, 0xA0), 2));
    p.drawLine(W / 2, 0, W / 2, H);
    p.drawLine(0, H / 2, W, H / 2);

    // ── Outer border ────────────────────────────────────────────────
    p.setPen(QPen(QColor(0xE0, 0xE0, 0xE8), 6));
    p.setBrush(Qt::NoBrush);
    p.drawRect(3, 3, W - 6, H - 6);

    // ── 90% safe-zone outline (dashed) ──────────────────────────────
    {
        QPen dash(QColor(0xC5, 0x8B, 0x4A), 2, Qt::DashLine);  // amber
        dash.setDashPattern({ 8, 6 });
        p.setPen(dash);
        const int mx = int(W * 0.05);
        const int my = int(H * 0.05);
        p.drawRect(mx, my, W - 2 * mx, H - 2 * my);
    }

    // ── Centre circle ───────────────────────────────────────────────
    p.setPen(QPen(QColor(0x6F, 0xAE, 0x63), 4));   // green; obvious if oblong
    p.setBrush(Qt::NoBrush);
    const int radius = std::min(W, H) / 4;
    p.drawEllipse(QPoint(W / 2, H / 2), radius, radius);

    // Cross at the centre.
    p.setPen(QPen(QColor(0xE0, 0xE0, 0xE8), 3));
    p.drawLine(W / 2 - 30, H / 2, W / 2 + 30, H / 2);
    p.drawLine(W / 2, H / 2 - 30, W / 2, H / 2 + 30);

    // ── Corner markers ──────────────────────────────────────────────
    QFont font;
    font.setPixelSize(56);
    font.setWeight(QFont::Black);
    p.setFont(font);
    p.setPen(QColor(0xE8, 0xC8, 0x61));   // yellow-ish

    constexpr int pad = 28;
    p.drawText(QPoint(pad,         pad + 56),   QStringLiteral("TL"));
    p.drawText(QPoint(W - pad - 96, pad + 56),  QStringLiteral("TR"));
    p.drawText(QPoint(W - pad - 96, H - pad),   QStringLiteral("BR"));
    p.drawText(QPoint(pad,         H - pad),    QStringLiteral("BL"));

    // ── Resolution label centred under the cross ────────────────────
    QFont small;
    small.setPixelSize(28);
    small.setWeight(QFont::DemiBold);
    p.setFont(small);
    p.setPen(QColor(0xB5, 0xAC, 0x9C));
    const QString label = QStringLiteral("quewi · projection mapping · %1×%2").arg(W).arg(H);
    p.drawText(QRect(0, H / 2 + radius + 24, W, 40),
               Qt::AlignHCenter | Qt::AlignTop, label);

    p.end();
    m_frame = std::move(img);
}

} // namespace quewi::video
