#include "ui/ParametricEqDialog.h"
#include "audio/effects/EqEffect.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui {

QColor ParametricEqDialog::bandColor(int i) {
    static const QColor colors[] = {
        QColor(255, 200,  60),  // band 1 — yellow
        QColor( 60, 220, 110),  // band 2 — green
        QColor( 80, 180, 255),  // band 3 — blue
    };
    if (i < 0 || i >= int(std::size(colors))) return Qt::white;
    return colors[i];
}

ParametricEqDialog::ParametricEqDialog(audio::EqEffect *eq, QWidget *parent)
    : QDialog(parent), m_eq(eq)
{
    setWindowTitle(tr("Parametric EQ"));
    setMinimumSize(820, 480);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Top: graph fills most of the dialog (paintEvent draws into it).
    // Bottom: numeric panel with one column per band.
    m_panel = new QWidget(this);
    m_panel->setFixedHeight(110);
    m_panel->setStyleSheet(QStringLiteral("background:#1A1F2A;"));

    vl->addStretch(1);             // graph area (we paint over this stretch)
    vl->addWidget(m_panel);

    rebuildBandPanel();

    if (m_eq) {
        connect(m_eq, &audio::AudioEffect::parameterChanged, this, [this](const QString &, float){
            updatePanel();
            update();
        });
    }
}

void ParametricEqDialog::resizeEvent(QResizeEvent *) {
    layoutGraph();
}

void ParametricEqDialog::layoutGraph() {
    m_graphRect = QRect(0, 0, width(), height() - m_panel->height());
    m_graphRect.adjust(40, 16, -16, -8); // margin for axes
}

// ── Coord helpers ─────────────────────────────────────────────────────────────

int ParametricEqDialog::freqToX(float hz) const {
    float t = std::log10(hz / kFreqMin) / std::log10(kFreqMax / kFreqMin);
    return m_graphRect.left() + int(t * m_graphRect.width());
}

float ParametricEqDialog::xToFreq(int x) const {
    float t = float(x - m_graphRect.left()) / float(m_graphRect.width());
    return kFreqMin * std::pow(kFreqMax / kFreqMin, t);
}

int ParametricEqDialog::gainToY(float dB) const {
    float t = (kGainMax - dB) / (kGainMax - kGainMin);
    return m_graphRect.top() + int(t * m_graphRect.height());
}

float ParametricEqDialog::yToGain(int y) const {
    float t = float(y - m_graphRect.top()) / float(m_graphRect.height());
    return kGainMax - t * (kGainMax - kGainMin);
}

int ParametricEqDialog::hitTestBand(const QPoint &p) const {
    if (!m_eq) return -1;
    for (int i = 0; i < audio::EqEffect::kNumBands; ++i) {
        auto b = m_eq->bandSnapshot(i);
        QPoint c(freqToX(b.freq), gainToY(b.gainDb));
        if ((p - c).manhattanLength() <= kHandleRadius * 2) return i;
    }
    return -1;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ParametricEqDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(20, 26, 38));

    if (m_graphRect.isNull()) layoutGraph();

    // Graph background
    QLinearGradient grad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    grad.setColorAt(0.0, QColor(28, 36, 52));
    grad.setColorAt(1.0, QColor(18, 24, 36));
    p.fillRect(m_graphRect, grad);

    // Frequency grid
    p.setPen(QColor(60, 70, 90));
    static const float kGridFreqs[] = {30,40,50,60,80,100,200,300,400,500,600,
                                       800,1000,2000,3000,4000,5000,6000,8000,
                                       10000,20000};
    for (float f : kGridFreqs) {
        int x = freqToX(f);
        if (x < m_graphRect.left() || x > m_graphRect.right()) continue;
        // Major lines on octaves
        bool major = (int(f) == 100 || int(f) == 1000 || int(f) == 10000);
        p.setPen(major ? QColor(85, 95, 115) : QColor(50, 58, 76));
        p.drawLine(x, m_graphRect.top(), x, m_graphRect.bottom());
    }
    // dB grid
    for (int dB = int(kGainMin); dB <= int(kGainMax); dB += 5) {
        int y = gainToY(float(dB));
        bool major = (dB == 0);
        p.setPen(major ? QColor(110, 120, 140) : QColor(50, 58, 76));
        p.drawLine(m_graphRect.left(), y, m_graphRect.right(), y);
    }

    // Axis labels
    p.setPen(QColor(150, 160, 180));
    p.setFont(QFont(font().family(), 8));
    static const float kLabelF[] = {30, 100, 300, 1000, 3000, 10000};
    for (float f : kLabelF) {
        int x = freqToX(f);
        QString lbl = (f >= 1000) ? QStringLiteral("%1k").arg(f / 1000.f, 0, 'g', 2)
                                  : QString::number(int(f));
        p.drawText(x - 12, m_graphRect.bottom() + 14, lbl);
    }
    for (int dB = -20; dB <= 20; dB += 10) {
        int y = gainToY(float(dB));
        p.drawText(m_graphRect.left() - 32, y + 4, QStringLiteral("%1").arg(dB));
    }

    if (!m_eq) return;

    // Compute composite response curve
    int nPts = m_graphRect.width();
    QVector<QPointF> curve;
    curve.reserve(nPts);
    QPainterPath fill;

    for (int i = 0; i < nPts; ++i) {
        int x = m_graphRect.left() + i;
        float f = xToFreq(x);
        float dB = m_eq->responseDb(f);
        int y = gainToY(dB);
        curve.append(QPointF(x, y));
    }

    // Filled area under curve
    int yZero = gainToY(0.f);
    fill.moveTo(curve.front().x(), yZero);
    for (auto &pt : curve) fill.lineTo(pt);
    fill.lineTo(curve.back().x(), yZero);
    fill.closeSubpath();

    QLinearGradient fillGrad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    fillGrad.setColorAt(0.0, QColor(150, 230, 200, 80));
    fillGrad.setColorAt(0.5, QColor(150, 230, 200, 30));
    fillGrad.setColorAt(1.0, QColor(150, 230, 200, 0));
    p.fillPath(fill, fillGrad);

    // Curve stroke
    p.setPen(QPen(QColor(140, 230, 200), 2.0));
    p.drawPolyline(curve.constData(), int(curve.size()));

    // Per-band light fills (Q-width hint)
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        auto sn = m_eq->bandSnapshot(b);
        if (std::abs(sn.gainDb) < 0.1f) continue;

        QColor c = bandColor(b);

        // Q-width box: ±freq*Q^-1 octave
        float bw = sn.freq / std::max(0.1f, sn.Q);
        int x1 = freqToX(std::max(kFreqMin, sn.freq - bw * 0.5f));
        int x2 = freqToX(std::min(kFreqMax, sn.freq + bw * 0.5f));
        int yT = std::min(gainToY(sn.gainDb), gainToY(0.f));
        int yB = std::max(gainToY(sn.gainDb), gainToY(0.f));

        QColor fillC = c;
        fillC.setAlpha(40);
        p.fillRect(QRect(x1, yT, x2 - x1, yB - yT), fillC);
    }

    // Band handles
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        auto sn = m_eq->bandSnapshot(b);
        QPoint c(freqToX(sn.freq), gainToY(sn.gainDb));
        QColor color = bandColor(b);

        // Halo
        if (b == m_hoverBand || b == m_dragBand) {
            QColor halo = color; halo.setAlpha(80);
            p.setBrush(halo); p.setPen(Qt::NoPen);
            p.drawEllipse(c, kHandleRadius * 2, kHandleRadius * 2);
        }
        p.setBrush(color);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(c, kHandleRadius, kHandleRadius);

        // Band number label
        p.setPen(Qt::black);
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        p.drawText(QRect(c.x() - kHandleRadius, c.y() - kHandleRadius,
                         kHandleRadius * 2, kHandleRadius * 2),
                   Qt::AlignCenter, QString::number(b + 1));
    }
}

// ── Mouse / wheel ─────────────────────────────────────────────────────────────

void ParametricEqDialog::mousePressEvent(QMouseEvent *e) {
    if (!m_eq) return;
    int b = hitTestBand(e->pos());
    if (b >= 0) m_dragBand = b;
}

void ParametricEqDialog::mouseMoveEvent(QMouseEvent *e) {
    if (!m_eq) return;
    int newHover = hitTestBand(e->pos());
    if (newHover != m_hoverBand) { m_hoverBand = newHover; update(); }

    if (m_dragBand >= 0 && m_graphRect.contains(e->pos())) {
        auto sn = m_eq->bandSnapshot(m_dragBand);
        float f = std::clamp(xToFreq(e->pos().x()), kFreqMin, kFreqMax);
        float g = std::clamp(yToGain(e->pos().y()), kGainMin, kGainMax);
        m_eq->setBand(m_dragBand, f, g, sn.Q);
    }
    setCursor(newHover >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void ParametricEqDialog::mouseReleaseEvent(QMouseEvent *) {
    m_dragBand = -1;
}

void ParametricEqDialog::wheelEvent(QWheelEvent *e) {
    if (!m_eq) return;
    int b = hitTestBand(e->position().toPoint());
    if (b < 0) return;
    auto sn = m_eq->bandSnapshot(b);
    float factor = (e->angleDelta().y() > 0) ? 1.15f : 1.f / 1.15f;
    float newQ = std::clamp(sn.Q * factor, 0.1f, 10.f);
    m_eq->setBand(b, sn.freq, sn.gainDb, newQ);
}

// ── Bottom panel ──────────────────────────────────────────────────────────────

void ParametricEqDialog::rebuildBandPanel() {
    auto *grid = new QGridLayout(m_panel);
    grid->setContentsMargins(40, 8, 40, 8);
    grid->setHorizontalSpacing(20);

    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        auto color = bandColor(b);
        auto *swatch = new QLabel(m_panel);
        swatch->setFixedSize(14, 14);
        swatch->setStyleSheet(QStringLiteral("background:%1; border-radius:7px;").arg(color.name()));

        auto *title = new QLabel(tr("Band %1").arg(b + 1), m_panel);
        title->setStyleSheet(QStringLiteral("color:#D8DCE4; font-weight:600;"));

        auto *header = new QWidget(m_panel);
        auto *hl = new QHBoxLayout(header);
        hl->setContentsMargins(0,0,0,0);
        hl->setSpacing(6);
        hl->addWidget(swatch);
        hl->addWidget(title);
        hl->addStretch();

        auto *freqL = new QLabel(QStringLiteral("Freq"), m_panel);
        auto *gainL = new QLabel(QStringLiteral("Gain"), m_panel);
        auto *qL    = new QLabel(QStringLiteral("Q"),    m_panel);
        for (auto *l : {freqL, gainL, qL})
            l->setStyleSheet(QStringLiteral("color:#A4ABBA; font-size:10px;"));

        auto sn = m_eq ? m_eq->bandSnapshot(b)
                       : audio::EqEffect::BandSnapshot{1000.f,0.f,0.707f,true};

        auto *freqSpin = new QDoubleSpinBox(m_panel);
        freqSpin->setRange(20.0, 20000.0); freqSpin->setDecimals(0);
        freqSpin->setSuffix(QStringLiteral(" Hz")); freqSpin->setValue(sn.freq);
        auto *gainSpin = new QDoubleSpinBox(m_panel);
        gainSpin->setRange(-24.0, 24.0); gainSpin->setDecimals(1);
        gainSpin->setSuffix(QStringLiteral(" dB")); gainSpin->setValue(sn.gainDb);
        auto *qSpin = new QDoubleSpinBox(m_panel);
        qSpin->setRange(0.1, 10.0); qSpin->setDecimals(2); qSpin->setSingleStep(0.1);
        qSpin->setValue(sn.Q);

        auto pushBand = [this, b, freqSpin, gainSpin, qSpin]() {
            if (!m_eq) return;
            m_eq->setBand(b, float(freqSpin->value()), float(gainSpin->value()), float(qSpin->value()));
        };
        connect(freqSpin, &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });
        connect(gainSpin, &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });
        connect(qSpin,    &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });

        // Track the spinboxes for updatePanel()
        freqSpin->setObjectName(QStringLiteral("freq%1").arg(b));
        gainSpin->setObjectName(QStringLiteral("gain%1").arg(b));
        qSpin   ->setObjectName(QStringLiteral("q%1").arg(b));

        int col = b;
        grid->addWidget(header,   0, col);
        grid->addWidget(freqL,    1, col);
        grid->addWidget(freqSpin, 2, col);
        grid->addWidget(gainL,    3, col);
        grid->addWidget(gainSpin, 4, col);
    }

    // Q row goes wide for compactness — actually let's lay out vertically per column.
    // (We added freq/gain rows; Q UI lives via wheel-on-graph.)
}

void ParametricEqDialog::updatePanel() {
    if (!m_eq || !m_panel) return;
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        auto sn = m_eq->bandSnapshot(b);
        if (auto *fs = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("freq%1").arg(b))) {
            QSignalBlocker sb(fs); fs->setValue(double(sn.freq));
        }
        if (auto *gs = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("gain%1").arg(b))) {
            QSignalBlocker sb(gs); gs->setValue(double(sn.gainDb));
        }
    }
}

} // namespace quewi::ui
