#include "ui/ParametricEqDialog.h"
#include "audio/effects/EqEffect.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui {

QColor ParametricEqDialog::bandColor(int i) {
    // Cool → warm spectrum, six steps. Picked for distinctness on the
    // dark graph background.
    static const QColor colors[] = {
        QColor( 80, 180, 255),  // 1 — sky blue (low shelf)
        QColor( 95, 220, 200),  // 2 — teal
        QColor( 90, 220, 110),  // 3 — green
        QColor(255, 200,  60),  // 4 — yellow
        QColor(255, 140,  90),  // 5 — orange
        QColor(255,  90, 160),  // 6 — pink (high shelf)
    };
    if (i < 0 || i >= int(std::size(colors))) return Qt::white;
    return colors[i];
}

ParametricEqDialog::ParametricEqDialog(audio::EqEffect *eq, QWidget *parent)
    : QDialog(parent), m_eq(eq)
{
    setWindowTitle(tr("Parametric EQ"));
    setMinimumSize(960, 560);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_panel = new QWidget(this);
    m_panel->setFixedHeight(160);
    m_panel->setStyleSheet(QStringLiteral("background:#181c22;"));

    vl->addStretch(1); // graph area (paintEvent draws here)
    vl->addWidget(m_panel);

    rebuildBandPanel();

    if (m_eq) {
        connect(m_eq, &audio::AudioEffect::parameterChanged, this,
                [this](const QString &, float) {
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
    m_graphRect.adjust(48, 18, -18, -22); // axis margins
}

// ── Coord helpers ────────────────────────────────────────────────────────────

int ParametricEqDialog::freqToX(float hz) const {
    const float t = std::log10(hz / kFreqMin) / std::log10(kFreqMax / kFreqMin);
    return m_graphRect.left() + int(t * m_graphRect.width());
}
float ParametricEqDialog::xToFreq(int x) const {
    const float t = float(x - m_graphRect.left()) / float(m_graphRect.width());
    return kFreqMin * std::pow(kFreqMax / kFreqMin, t);
}
int ParametricEqDialog::gainToY(float dB) const {
    const float t = (kGainMax - dB) / (kGainMax - kGainMin);
    return m_graphRect.top() + int(t * m_graphRect.height());
}
float ParametricEqDialog::yToGain(int y) const {
    const float t = float(y - m_graphRect.top()) / float(m_graphRect.height());
    return kGainMax - t * (kGainMax - kGainMin);
}

int ParametricEqDialog::hitTestBand(const QPoint &p) const {
    if (!m_eq) return -1;
    int best = -1; int bestDist = kHandleRadius * 3;
    for (int i = 0; i < audio::EqEffect::kNumBands; ++i) {
        const auto b = m_eq->bandSnapshot(i);
        const QPoint c(freqToX(b.freq), gainToY(b.gainDb));
        const int d = (p - c).manhattanLength();
        if (d <= bestDist) { best = i; bestDist = d; }
    }
    return best;
}

// ── Paint ────────────────────────────────────────────────────────────────────

void ParametricEqDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Window background (Stitch bgPanel).
    p.fillRect(rect(), QColor(0x18, 0x1c, 0x22));
    if (m_graphRect.isNull()) layoutGraph();

    // Graph background — slight vertical gradient.
    QLinearGradient grad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    grad.setColorAt(0.0, QColor(0x1c, 0x20, 0x26));
    grad.setColorAt(1.0, QColor(0x10, 0x14, 0x19));
    p.fillRect(m_graphRect, grad);

    // Frame
    p.setPen(QColor(0x41, 0x47, 0x52));
    p.drawRect(m_graphRect.adjusted(0, 0, -1, -1));

    // ── Frequency grid ──────────────────────────────────────────────
    static const float kGridFreqs[] = {
        30,40,50,60,80,100,200,300,400,500,600,800,1000,
        2000,3000,4000,5000,6000,8000,10000,20000
    };
    for (float f : kGridFreqs) {
        const int x = freqToX(f);
        if (x < m_graphRect.left() || x > m_graphRect.right()) continue;
        const bool major = (int(f) == 100 || int(f) == 1000 || int(f) == 10000);
        p.setPen(major ? QColor(0x41, 0x47, 0x52) : QColor(0x26, 0x2a, 0x38));
        p.drawLine(x, m_graphRect.top(), x, m_graphRect.bottom());
    }
    // dB grid
    for (int dB = int(kGainMin); dB <= int(kGainMax); dB += 3) {
        const int y = gainToY(float(dB));
        const bool major = (dB == 0);
        const bool half  = (dB % 6 == 0);
        QColor c = major ? QColor(0x8a, 0x91, 0x9e)
                  : half  ? QColor(0x41, 0x47, 0x52)
                          : QColor(0x26, 0x2a, 0x38);
        p.setPen(c);
        p.drawLine(m_graphRect.left(), y, m_graphRect.right(), y);
    }

    // ── Axis labels ─────────────────────────────────────────────────
    p.setPen(QColor(0x8a, 0x91, 0x9e));
    p.setFont(QFont(font().family(), 8));
    static const float kLabelF[] = {30, 100, 300, 1000, 3000, 10000};
    for (float f : kLabelF) {
        const int x = freqToX(f);
        const QString lbl = (f >= 1000)
            ? QStringLiteral("%1k").arg(f / 1000.f, 0, 'g', 2)
            : QString::number(int(f));
        p.drawText(x - 12, m_graphRect.bottom() + 14, lbl);
    }
    for (int dB = -24; dB <= 24; dB += 6) {
        const int y = gainToY(float(dB));
        p.drawText(m_graphRect.left() - 36, y + 4,
                   QStringLiteral("%1").arg(dB > 0 ? QStringLiteral("+%1").arg(dB)
                                                  : QString::number(dB)));
    }

    if (!m_eq) return;

    // ── Per-band ghost curves ───────────────────────────────────────
    // Render each band's individual contribution as a thin coloured
    // curve, alpha-faded if the band is bypassed. Lets the operator see
    // exactly which band is doing what.
    const int yZero = gainToY(0.f);
    const int nPts = m_graphRect.width();
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        const auto sn = m_eq->bandSnapshot(b);
        QVector<QPointF> bandCurve;
        bandCurve.reserve(nPts);
        bool nonTrivial = false;
        for (int i = 0; i < nPts; ++i) {
            const int x = m_graphRect.left() + i;
            const float f  = xToFreq(x);
            const float dB = m_eq->bandResponseDb(b, f);
            if (std::abs(dB) > 0.05f) nonTrivial = true;
            bandCurve.append(QPointF(x, gainToY(std::clamp(dB, kGainMin, kGainMax))));
        }
        if (!nonTrivial) continue;
        QColor c = bandColor(b);
        c.setAlpha(sn.enabled ? 150 : 60);
        QPen pen(c); pen.setWidthF(1.2);
        if (!sn.enabled) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawPolyline(bandCurve.constData(), int(bandCurve.size()));
    }

    // ── Composite curve + filled area ──────────────────────────────
    QVector<QPointF> curve;
    curve.reserve(nPts);
    for (int i = 0; i < nPts; ++i) {
        const int x = m_graphRect.left() + i;
        const float f  = xToFreq(x);
        const float dB = m_eq->responseDb(f);
        curve.append(QPointF(x, gainToY(std::clamp(dB, kGainMin, kGainMax))));
    }
    QPainterPath fill;
    fill.moveTo(curve.front().x(), yZero);
    for (auto &pt : curve) fill.lineTo(pt);
    fill.lineTo(curve.back().x(), yZero);
    fill.closeSubpath();

    QLinearGradient fillGrad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    fillGrad.setColorAt(0.0, QColor(0xa4, 0xc9, 0xff, 70));
    fillGrad.setColorAt(0.5, QColor(0xa4, 0xc9, 0xff, 25));
    fillGrad.setColorAt(1.0, QColor(0xa4, 0xc9, 0xff, 0));
    p.fillPath(fill, fillGrad);

    QPen curvePen(QColor(0xa4, 0xc9, 0xff)); curvePen.setWidthF(2.0);
    p.setPen(curvePen);
    p.drawPolyline(curve.constData(), int(curve.size()));

    // ── Band handles ───────────────────────────────────────────────
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        const auto sn = m_eq->bandSnapshot(b);
        const QPoint c(freqToX(sn.freq), gainToY(sn.gainDb));
        QColor color = bandColor(b);
        if (!sn.enabled) color.setAlpha(120);

        if (b == m_hoverBand || b == m_dragBand) {
            QColor halo = color; halo.setAlpha(70);
            p.setBrush(halo); p.setPen(Qt::NoPen);
            p.drawEllipse(c, kHandleRadius * 2, kHandleRadius * 2);
        }
        p.setBrush(color);
        p.setPen(QPen(QColor(0x10, 0x14, 0x19), 2));
        p.drawEllipse(c, kHandleRadius, kHandleRadius);

        p.setPen(QColor(0x10, 0x14, 0x19));
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        p.drawText(QRect(c.x() - kHandleRadius, c.y() - kHandleRadius,
                         kHandleRadius * 2, kHandleRadius * 2),
                   Qt::AlignCenter, QString::number(b + 1));
    }

    // ── Cursor crosshair + readout ─────────────────────────────────
    if (m_graphRect.contains(m_cursor)) {
        p.setPen(QPen(QColor(0xc0, 0xc7, 0xd4, 160), 1, Qt::DashLine));
        p.drawLine(m_cursor.x(), m_graphRect.top(), m_cursor.x(), m_graphRect.bottom());
        p.drawLine(m_graphRect.left(), m_cursor.y(), m_graphRect.right(), m_cursor.y());

        const float f  = xToFreq(m_cursor.x());
        const float dB = m_eq->responseDb(f);
        const QString fStr = (f >= 1000.f)
            ? QStringLiteral("%1 kHz").arg(f / 1000.f, 0, 'f', 2)
            : QStringLiteral("%1 Hz").arg(int(f));
        const QString dBStr = QStringLiteral("%1 dB")
            .arg(dB >= 0 ? QStringLiteral("+%1").arg(dB, 0, 'f', 1)
                         : QString::number(dB, 'f', 1));
        const QString readout = QStringLiteral("%1   %2").arg(fStr, dBStr);
        p.setFont(QFont(QStringLiteral("Space Grotesk"), 10, QFont::Medium));
        const QFontMetrics fm(p.font());
        const QSize sz = fm.size(0, readout) + QSize(14, 8);
        const int rx = std::min(m_cursor.x() + 12, m_graphRect.right() - sz.width());
        const int ry = std::max(m_graphRect.top() + 4, m_cursor.y() - sz.height() - 6);
        const QRect rrect(rx, ry, sz.width(), sz.height());
        p.fillRect(rrect, QColor(0x10, 0x14, 0x19, 220));
        p.setPen(QColor(0x41, 0x47, 0x52));
        p.drawRect(rrect.adjusted(0,0,-1,-1));
        p.setPen(QColor(0xe0, 0xe2, 0xeb));
        p.drawText(rrect, Qt::AlignCenter, readout);
    }
}

// ── Mouse / wheel ────────────────────────────────────────────────────────────

void ParametricEqDialog::mousePressEvent(QMouseEvent *e) {
    if (!m_eq) return;
    const int b = hitTestBand(e->pos());
    if (b >= 0) m_dragBand = b;
}

void ParametricEqDialog::mouseMoveEvent(QMouseEvent *e) {
    m_cursor = e->pos();
    if (!m_eq) { update(); return; }
    const int newHover = hitTestBand(e->pos());
    if (newHover != m_hoverBand) { m_hoverBand = newHover; }

    if (m_dragBand >= 0 && m_graphRect.contains(e->pos())) {
        const auto sn = m_eq->bandSnapshot(m_dragBand);
        const float f = std::clamp(xToFreq(e->pos().x()), kFreqMin, kFreqMax);
        const float g = std::clamp(yToGain(e->pos().y()), kGainMin, kGainMax);
        m_eq->setBand(m_dragBand, f, g, sn.Q);
    }
    setCursor(newHover >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void ParametricEqDialog::mouseReleaseEvent(QMouseEvent *) {
    m_dragBand = -1;
}

void ParametricEqDialog::mouseDoubleClickEvent(QMouseEvent *e) {
    if (!m_eq) return;
    const int b = hitTestBand(e->pos());
    if (b < 0) return;
    const auto sn = m_eq->bandSnapshot(b);
    // Reset the band to flat at the same frequency.
    m_eq->setBand(b, sn.freq, 0.f, 0.707f);
}

void ParametricEqDialog::wheelEvent(QWheelEvent *e) {
    if (!m_eq) return;
    const int b = hitTestBand(e->position().toPoint());
    if (b < 0) return;
    const auto sn = m_eq->bandSnapshot(b);
    const float factor = (e->angleDelta().y() > 0) ? 1.15f : 1.f / 1.15f;
    const float newQ = std::clamp(sn.Q * factor, 0.1f, 10.f);
    m_eq->setBand(b, sn.freq, sn.gainDb, newQ);
}

void ParametricEqDialog::leaveEvent(QEvent *) {
    m_cursor = QPoint(-1, -1);
    update();
}

// ── Bottom panel ─────────────────────────────────────────────────────────────

namespace {
QStringList filterTypeNames() {
    return { QStringLiteral("Peak"), QStringLiteral("Lo Shelf"),
             QStringLiteral("Hi Shelf"), QStringLiteral("Lo Pass"),
             QStringLiteral("Hi Pass") };
}
}

void ParametricEqDialog::rebuildBandPanel() {
    auto *grid = new QGridLayout(m_panel);
    grid->setContentsMargins(48, 10, 18, 10);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);

    const QString lblStyle = QStringLiteral(
        "color:#8a919e; font-size:10px; font-weight:700; letter-spacing:0.10em;");
    const QString spinStyle = QStringLiteral(
        "QDoubleSpinBox, QComboBox { background:#1c2026; color:#e0e2eb;"
        "  border:1px solid #414752; padding:2px 4px; min-height:20px; }"
        "QDoubleSpinBox:focus, QComboBox:focus { border:1px solid #4a9eff; }");

    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        const auto color = bandColor(b);
        auto sn = m_eq ? m_eq->bandSnapshot(b)
                       : audio::EqEffect::BandSnapshot{1000.f, 0.f, 0.707f,
                                                        audio::EqEffect::Peaking, true};

        // Header row: swatch + "Band N" + enable checkbox
        auto *header = new QWidget(m_panel);
        auto *hl = new QHBoxLayout(header);
        hl->setContentsMargins(0,0,0,0); hl->setSpacing(6);

        auto *swatch = new QLabel(header);
        swatch->setFixedSize(10, 10);
        swatch->setStyleSheet(QStringLiteral(
            "background:%1; border-radius:5px;").arg(color.name()));

        auto *title = new QLabel(tr("BAND %1").arg(b + 1), header);
        title->setStyleSheet(QStringLiteral(
            "color:#e0e2eb; font-size:11px; font-weight:700; letter-spacing:0.10em;"));

        auto *enableBox = new QCheckBox(header);
        enableBox->setObjectName(QStringLiteral("on%1").arg(b));
        enableBox->setChecked(sn.enabled);
        enableBox->setToolTip(tr("Bypass this band"));
        connect(enableBox, &QCheckBox::toggled, this, [this, b](bool on) {
            if (m_eq) m_eq->setBandEnabled(b, on);
            update();
        });

        hl->addWidget(swatch); hl->addWidget(title); hl->addStretch(); hl->addWidget(enableBox);

        // Type combo
        auto *typeL = new QLabel(QStringLiteral("TYPE"), m_panel);
        typeL->setStyleSheet(lblStyle);
        auto *typeCombo = new QComboBox(m_panel);
        typeCombo->setObjectName(QStringLiteral("type%1").arg(b));
        typeCombo->addItems(filterTypeNames());
        typeCombo->setCurrentIndex(int(sn.type));
        typeCombo->setStyleSheet(spinStyle);
        connect(typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, b](int idx) {
            if (m_eq) m_eq->setBandType(b, audio::EqEffect::FilterType(idx));
            update();
        });

        // Numeric controls
        auto *freqL = new QLabel(QStringLiteral("FREQ"), m_panel);    freqL->setStyleSheet(lblStyle);
        auto *gainL = new QLabel(QStringLiteral("GAIN"), m_panel);    gainL->setStyleSheet(lblStyle);
        auto *qL    = new QLabel(QStringLiteral("Q"),    m_panel);    qL->setStyleSheet(lblStyle);

        auto *freqSpin = new QDoubleSpinBox(m_panel);
        freqSpin->setObjectName(QStringLiteral("freq%1").arg(b));
        freqSpin->setRange(20.0, 20000.0); freqSpin->setDecimals(0);
        freqSpin->setSuffix(QStringLiteral(" Hz")); freqSpin->setValue(sn.freq);
        freqSpin->setStyleSheet(spinStyle);

        auto *gainSpin = new QDoubleSpinBox(m_panel);
        gainSpin->setObjectName(QStringLiteral("gain%1").arg(b));
        gainSpin->setRange(-24.0, 24.0); gainSpin->setDecimals(1); gainSpin->setSingleStep(0.5);
        gainSpin->setSuffix(QStringLiteral(" dB")); gainSpin->setValue(sn.gainDb);
        gainSpin->setStyleSheet(spinStyle);

        auto *qSpin = new QDoubleSpinBox(m_panel);
        qSpin->setObjectName(QStringLiteral("q%1").arg(b));
        qSpin->setRange(0.1, 10.0); qSpin->setDecimals(2); qSpin->setSingleStep(0.1);
        qSpin->setValue(sn.Q);
        qSpin->setStyleSheet(spinStyle);

        auto pushBand = [this, b, freqSpin, gainSpin, qSpin]() {
            if (!m_eq) return;
            m_eq->setBand(b, float(freqSpin->value()), float(gainSpin->value()),
                          float(qSpin->value()));
        };
        connect(freqSpin, &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });
        connect(gainSpin, &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });
        connect(qSpin,    &QDoubleSpinBox::valueChanged, this, [pushBand](double){ pushBand(); });

        // Place the column.
        const int col = b;
        grid->addWidget(header,    0, col);
        grid->addWidget(typeL,     1, col);
        grid->addWidget(typeCombo, 2, col);
        grid->addWidget(freqL,     3, col);
        grid->addWidget(freqSpin,  4, col);
        grid->addWidget(gainL,     5, col);
        grid->addWidget(gainSpin,  6, col);
        grid->addWidget(qL,        7, col);
        grid->addWidget(qSpin,     8, col);
    }
}

void ParametricEqDialog::updatePanel() {
    if (!m_eq || !m_panel) return;
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        const auto sn = m_eq->bandSnapshot(b);
        if (auto *fs = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("freq%1").arg(b))) {
            QSignalBlocker sb(fs); fs->setValue(double(sn.freq));
        }
        if (auto *gs = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("gain%1").arg(b))) {
            QSignalBlocker sb(gs); gs->setValue(double(sn.gainDb));
        }
        if (auto *qs = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("q%1").arg(b))) {
            QSignalBlocker sb(qs); qs->setValue(double(sn.Q));
        }
        if (auto *tc = m_panel->findChild<QComboBox*>(QStringLiteral("type%1").arg(b))) {
            QSignalBlocker sb(tc); tc->setCurrentIndex(int(sn.type));
        }
        if (auto *en = m_panel->findChild<QCheckBox*>(QStringLiteral("on%1").arg(b))) {
            QSignalBlocker sb(en); en->setChecked(sn.enabled);
        }
    }
}

} // namespace quewi::ui
