#pragma once

#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace quewi::ui {

// Tiny vertical audio level meter: green → yellow → red with a peak-hold tick.
// Header-only and intentionally NOT a Q_OBJECT — it has no signals/slots, just
// a setLevel() setter and a custom paint, so it needs no moc. Feed it a linear
// 0..1 level (peak of the current buffer); it applies fast-attack / slow-
// release ballistics so it reads like a hardware meter.
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(16);
        setMinimumHeight(80);
    }

    void setLevel(float linear) {
        linear = std::clamp(linear, 0.f, 1.f);
        if (linear > m_level) m_level = linear;                 // fast attack
        else                  m_level = m_level * 0.78f + linear * 0.22f; // slow release
        if (linear > m_peak)  m_peak = linear;
        else                  m_peak = std::max(linear, m_peak - 0.015f);
        update();
    }

    void reset() { m_level = 0.f; m_peak = 0.f; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.fillRect(r, QColor(0x10, 0x14, 0x19));
        p.setPen(QColor(0x33, 0x39, 0x44));
        p.drawRect(r);

        const int h = r.height();
        const int filled = int(m_level * float(h));
        if (filled > 0) {
            QLinearGradient g(0, r.bottom(), 0, r.top());
            g.setColorAt(0.00, QColor(0x5f, 0xd9, 0x6a)); // green — quiet
            g.setColorAt(0.60, QColor(0xf2, 0xd6, 0x44)); // yellow
            g.setColorAt(0.85, QColor(0xff, 0x9b, 0x3d)); // orange
            g.setColorAt(1.00, QColor(0xff, 0x46, 0x46)); // red — peaking
            p.fillRect(QRect(r.left() + 1, r.bottom() - filled + 1,
                             r.width() - 1, filled), g);
        }
        if (m_peak > 0.001f) {
            const int py = r.bottom() - int(m_peak * float(h));
            p.setPen(QColor(0xff, 0xff, 0xff, 170));
            p.drawLine(r.left() + 1, py, r.right(), py);
        }
    }

private:
    float m_level = 0.f;
    float m_peak  = 0.f;
};

} // namespace quewi::ui
