#include "ui/CompressorDialog.h"
#include "audio/effects/CompressorEffect.h"
#include "ui/Theme.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace quewi::ui {

CompressorDialog::CompressorDialog(audio::CompressorEffect *comp, QWidget *parent)
    : QDialog(parent), m_comp(comp)
{
    setWindowTitle(tr("Compressor"));
    setMinimumSize(720, 520);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_panel = new QWidget(this);
    m_panel->setFixedHeight(96);
    m_panel->setStyleSheet(QStringLiteral("background:%1;")
                               .arg(Theme::tokens().bgDeep.name()));

    vl->addStretch(1); // graph area (paintEvent draws here)
    vl->addWidget(m_panel);

    buildPanel();

    if (m_comp) {
        connect(m_comp, &audio::AudioEffect::parameterChanged, this,
                [this](const QString &, float) { syncPanel(); update(); });
    }

    // ~30 Hz poll of the live gain-reduction value. Repaints only when the
    // meter actually moved so the dialog sits idle when nothing is playing.
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);
    connect(m_meterTimer, &QTimer::timeout, this, [this] {
        if (!m_comp) return;
        const float gr = m_comp->currentGainReductionDb();
        if (std::abs(gr - m_lastMeterDb) > 0.05f) {
            m_lastMeterDb = gr;
            update();
        }
    });
    m_meterTimer->start();
}

void CompressorDialog::resizeEvent(QResizeEvent *) { layoutGraph(); }

void CompressorDialog::layoutGraph() {
    m_graphRect = QRect(0, 0, width(), height() - m_panel->height());
    // Left margin for dB labels, right margin for the GR meter + labels.
    m_graphRect.adjust(48, 18, -(kMeterWidth + 44), -26);
}

// ── Coord helpers ────────────────────────────────────────────────────────────

int CompressorDialog::inToX(float dB) const {
    const float t = (dB - kInMin) / (kInMax - kInMin);
    return m_graphRect.left() + int(t * m_graphRect.width());
}
float CompressorDialog::xToIn(int x) const {
    const float t = float(x - m_graphRect.left()) / float(std::max(1, m_graphRect.width()));
    return kInMin + t * (kInMax - kInMin);
}
int CompressorDialog::outToY(float dB) const {
    const float t = (kOutMax - dB) / (kOutMax - kOutMin);
    return m_graphRect.top() + int(t * m_graphRect.height());
}
float CompressorDialog::yToOut(int y) const {
    const float t = float(y - m_graphRect.top()) / float(std::max(1, m_graphRect.height()));
    return kOutMax - t * (kOutMax - kOutMin);
}

QPoint CompressorDialog::thresholdHandlePos() const {
    if (!m_comp) return {};
    const float thr = m_comp->thresholdDb();
    return QPoint(inToX(thr),
                  outToY(std::clamp(m_comp->transferOutputDb(thr), kOutMin, kOutMax)));
}
QPoint CompressorDialog::ratioHandlePos() const {
    if (!m_comp) return {};
    return QPoint(inToX(kInMax),
                  outToY(std::clamp(m_comp->transferOutputDb(kInMax), kOutMin, kOutMax)));
}

int CompressorDialog::hitTestHandle(const QPoint &p) const {
    if (!m_comp) return 0;
    if ((p - thresholdHandlePos()).manhattanLength() <= kHandleRadius * 3) return 1;
    if ((p - ratioHandlePos()).manhattanLength()     <= kHandleRadius * 3) return 2;
    return 0;
}

void CompressorDialog::setRatioFromOutputAt0(float outDb) {
    if (!m_comp) return;
    const float thr    = m_comp->thresholdDb();
    const float makeup = m_comp->makeupDb();
    const float over   = kInMax - thr;          // dB above threshold at 0 dBFS
    if (over <= 0.01f) return;                   // threshold at/above 0 → ratio irrelevant
    const float grAt0 = std::min(0.f, outDb - makeup); // reduction (≤ 0)
    const float k = grAt0 / over + 1.f;          // = 1/ratio
    float ratio;
    if (k <= 0.05f) ratio = 20.f;                // brick-wall limit
    else            ratio = 1.f / k;
    ratio = std::clamp(ratio, 1.f, 20.f);
    m_comp->setParameterValue(QStringLiteral("ratio"), ratio);
}

// ── Paint ────────────────────────────────────────────────────────────────────

void CompressorDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(0x18, 0x1c, 0x22));
    if (m_graphRect.isNull()) layoutGraph();

    // Graph background — slight vertical gradient.
    QLinearGradient grad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    grad.setColorAt(0.0, QColor(0x1c, 0x20, 0x26));
    grad.setColorAt(1.0, QColor(0x10, 0x14, 0x19));
    p.fillRect(m_graphRect, grad);
    p.setPen(QColor(0x41, 0x47, 0x52));
    p.drawRect(m_graphRect.adjusted(0, 0, -1, -1));

    // ── dB grid (both axes share dB units) ──────────────────────────
    p.setFont(QFont(font().family(), 8));
    for (int dB = -60; dB <= 0; dB += 6) {
        const int x = inToX(float(dB));
        const bool major = (dB == 0 || dB == -60);
        p.setPen(major ? QColor(0x41, 0x47, 0x52) : QColor(0x26, 0x2a, 0x38));
        p.drawLine(x, m_graphRect.top(), x, m_graphRect.bottom());
        p.setPen(QColor(0x8a, 0x91, 0x9e));
        p.drawText(x - 10, m_graphRect.bottom() + 16, QString::number(dB));
    }
    for (int dB = -60; dB <= 12; dB += 6) {
        const int y = outToY(float(dB));
        const bool major = (dB == 0);
        p.setPen(major ? QColor(0x8a, 0x91, 0x9e) : QColor(0x26, 0x2a, 0x38));
        p.drawLine(m_graphRect.left(), y, m_graphRect.right(), y);
        p.setPen(QColor(0x8a, 0x91, 0x9e));
        p.drawText(m_graphRect.left() - 36, y + 4,
                   dB > 0 ? QStringLiteral("+%1").arg(dB) : QString::number(dB));
    }

    // Axis captions
    p.setPen(QColor(0x6b, 0x72, 0x80));
    p.setFont(QFont(font().family(), 8, QFont::DemiBold));
    p.drawText(QRect(m_graphRect.left(), m_graphRect.bottom() + 18, m_graphRect.width(), 14),
               Qt::AlignCenter, tr("INPUT  (dBFS)"));

    if (!m_comp) return;

    // ── 1:1 reference (no compression, no makeup) ───────────────────
    {
        QPen ref(QColor(0x55, 0x5c, 0x6b)); ref.setStyle(Qt::DashLine); ref.setWidthF(1.0);
        p.setPen(ref);
        p.drawLine(inToX(kInMin), outToY(std::clamp(kInMin, kOutMin, kOutMax)),
                   inToX(kInMax), outToY(std::clamp(kInMax, kOutMin, kOutMax)));
    }

    // ── Transfer curve + filled area ────────────────────────────────
    const int nPts = m_graphRect.width();
    QVector<QPointF> curve;
    curve.reserve(nPts);
    for (int i = 0; i < nPts; ++i) {
        const int x   = m_graphRect.left() + i;
        const float in  = xToIn(x);
        const float out = std::clamp(m_comp->transferOutputDb(in), kOutMin, kOutMax);
        curve.append(QPointF(x, outToY(out)));
    }
    QPainterPath fill;
    fill.moveTo(curve.front().x(), m_graphRect.bottom());
    for (auto &pt : curve) fill.lineTo(pt);
    fill.lineTo(curve.back().x(), m_graphRect.bottom());
    fill.closeSubpath();
    QLinearGradient fg(0, m_graphRect.top(), 0, m_graphRect.bottom());
    fg.setColorAt(0.0, QColor(0xa4, 0xc9, 0xff, 60));
    fg.setColorAt(1.0, QColor(0xa4, 0xc9, 0xff, 0));
    p.fillPath(fill, fg);

    QPen curvePen(QColor(0xa4, 0xc9, 0xff)); curvePen.setWidthF(2.0);
    p.setPen(curvePen);
    p.drawPolyline(curve.constData(), int(curve.size()));

    // Threshold guide line
    {
        const int tx = inToX(m_comp->thresholdDb());
        QPen tp(QColor(0xff, 0xc8, 0x4a, 150)); tp.setStyle(Qt::DashLine);
        p.setPen(tp);
        p.drawLine(tx, m_graphRect.top(), tx, m_graphRect.bottom());
    }

    // ── Handles ─────────────────────────────────────────────────────
    auto drawHandle = [&](const QPoint &c, const QColor &col, bool active) {
        if (active) {
            QColor halo = col; halo.setAlpha(70);
            p.setBrush(halo); p.setPen(Qt::NoPen);
            p.drawEllipse(c, kHandleRadius * 2, kHandleRadius * 2);
        }
        p.setBrush(col);
        p.setPen(QPen(QColor(0x10, 0x14, 0x19), 2));
        p.drawEllipse(c, kHandleRadius, kHandleRadius);
    };
    drawHandle(thresholdHandlePos(), QColor(0xff, 0xc8, 0x4a),
               m_hoverHandle == 1 || m_drag == Drag::Threshold);
    drawHandle(ratioHandlePos(), QColor(0x5f, 0xdc, 0xc8),
               m_hoverHandle == 2 || m_drag == Drag::Ratio);

    // ── Gain-reduction meter ────────────────────────────────────────
    const int mx = m_graphRect.right() + 18;
    const QRect meter(mx, m_graphRect.top(), kMeterWidth, m_graphRect.height());
    p.fillRect(meter, QColor(0x10, 0x14, 0x19));
    p.setPen(QColor(0x41, 0x47, 0x52));
    p.drawRect(meter.adjusted(0, 0, -1, -1));
    const float gr = std::clamp(-m_comp->currentGainReductionDb(), 0.f, kMeterRange);
    const int barH = int(gr / kMeterRange * meter.height());
    if (barH > 0) {
        QLinearGradient mg(0, meter.top(), 0, meter.bottom());
        mg.setColorAt(0.0, QColor(0xff, 0x6b, 0x6b));
        mg.setColorAt(0.5, QColor(0xff, 0xc8, 0x4a));
        mg.setColorAt(1.0, QColor(0x5f, 0xdc, 0xc8));
        p.fillRect(QRect(meter.left() + 1, meter.top() + 1,
                         meter.width() - 2, barH), mg);
    }
    p.setPen(QColor(0x8a, 0x91, 0x9e));
    p.setFont(QFont(font().family(), 7, QFont::DemiBold));
    p.drawText(QRect(mx - 6, meter.bottom() + 4, kMeterWidth + 30, 12),
               Qt::AlignLeft, tr("GR"));
    p.drawText(QRect(mx - 14, m_graphRect.top() - 16, kMeterWidth + 40, 12),
               Qt::AlignCenter, QStringLiteral("-%1").arg(gr, 0, 'f', 1));

    // ── Cursor readout ──────────────────────────────────────────────
    if (m_graphRect.contains(m_cursor)) {
        p.setPen(QPen(QColor(0xc0, 0xc7, 0xd4, 140), 1, Qt::DashLine));
        p.drawLine(m_cursor.x(), m_graphRect.top(), m_cursor.x(), m_graphRect.bottom());
        const float in  = xToIn(m_cursor.x());
        const float out = m_comp->transferOutputDb(in);
        const QString readout =
            QStringLiteral("in %1   out %2")
                .arg(in,  0, 'f', 1)
                .arg(out, 0, 'f', 1);
        p.setFont(QFont(QStringLiteral("Space Grotesk"), 9, QFont::Medium));
        const QFontMetrics fm(p.font());
        const QSize sz = fm.size(0, readout) + QSize(14, 8);
        const int rx = std::min(m_cursor.x() + 12, m_graphRect.right() - sz.width());
        const int ry = m_graphRect.top() + 4;
        const QRect rrect(rx, ry, sz.width(), sz.height());
        p.fillRect(rrect, QColor(0x10, 0x14, 0x19, 220));
        p.setPen(QColor(0x41, 0x47, 0x52));
        p.drawRect(rrect.adjusted(0, 0, -1, -1));
        p.setPen(QColor(0xe0, 0xe2, 0xeb));
        p.drawText(rrect, Qt::AlignCenter, readout);
    }
}

// ── Mouse / wheel ────────────────────────────────────────────────────────────

void CompressorDialog::mousePressEvent(QMouseEvent *e) {
    if (!m_comp) return;
    const int h = hitTestHandle(e->pos());
    if (h == 1)      m_drag = Drag::Threshold;
    else if (h == 2) m_drag = Drag::Ratio;
}

void CompressorDialog::mouseMoveEvent(QMouseEvent *e) {
    m_cursor = e->pos();
    if (!m_comp) { update(); return; }

    if (m_drag == Drag::Threshold) {
        const float thr = std::clamp(xToIn(e->pos().x()), kInMin, kInMax);
        m_comp->setParameterValue(QStringLiteral("threshold"), thr);
    } else if (m_drag == Drag::Ratio) {
        setRatioFromOutputAt0(yToOut(e->pos().y()));
    } else {
        const int newHover = hitTestHandle(e->pos());
        if (newHover != m_hoverHandle) m_hoverHandle = newHover;
        setCursor(newHover ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    update();
}

void CompressorDialog::mouseReleaseEvent(QMouseEvent *) { m_drag = Drag::None; }

void CompressorDialog::mouseDoubleClickEvent(QMouseEvent *) {
    if (!m_comp) return;
    m_comp->setParameterValue(QStringLiteral("threshold"),
                              m_comp->parameterDefault(QStringLiteral("threshold")));
    m_comp->setParameterValue(QStringLiteral("ratio"),
                              m_comp->parameterDefault(QStringLiteral("ratio")));
    m_comp->setParameterValue(QStringLiteral("knee"),
                              m_comp->parameterDefault(QStringLiteral("knee")));
    m_comp->setParameterValue(QStringLiteral("makeup"),
                              m_comp->parameterDefault(QStringLiteral("makeup")));
}

void CompressorDialog::wheelEvent(QWheelEvent *e) {
    if (!m_comp) return;
    const float step = (e->angleDelta().y() > 0) ? 1.f : -1.f;
    const float knee = std::clamp(m_comp->kneeDb() + step, 0.f, 12.f);
    m_comp->setParameterValue(QStringLiteral("knee"), knee);
}

void CompressorDialog::leaveEvent(QEvent *) {
    m_cursor = QPoint(-1, -1);
    m_hoverHandle = 0;
    update();
}

// ── Numeric panel ────────────────────────────────────────────────────────────

void CompressorDialog::buildPanel() {
    auto *grid = new QGridLayout(m_panel);
    grid->setContentsMargins(48, 10, 18, 10);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);

    const auto &tk = Theme::tokens();
    const QString lblStyle = QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.10em;")
        .arg(tk.ink40.name());
    const QString spinStyle = QStringLiteral(
        "QDoubleSpinBox { background:%1; color:%2; border:1px solid %3;"
        "  padding:2px 4px; min-height:22px; }"
        "QDoubleSpinBox:focus { border:1px solid %4; }")
        .arg(tk.bgInteractive.name(), tk.ink100.name(),
             tk.outline.name(), tk.outlineFocus.name());

    struct Ctl { const char *id; const char *label; const char *suffix; };
    static const Ctl ctls[] = {
        {"threshold", "THRESHOLD", " dB"},
        {"ratio",     "RATIO",     ":1"},
        {"knee",      "KNEE",      " dB"},
        {"attack",    "ATTACK",    " ms"},
        {"release",   "RELEASE",   " ms"},
        {"makeup",    "MAKEUP",    " dB"},
    };

    int col = 0;
    for (const auto &c : ctls) {
        const QString id = QString::fromLatin1(c.id);
        auto [lo, hi] = m_comp ? m_comp->parameterRange(id) : QPair<float,float>{0.f, 1.f};
        const int dec = m_comp ? m_comp->parameterDecimals(id) : 1;

        auto *lab = new QLabel(QString::fromLatin1(c.label), m_panel);
        lab->setStyleSheet(lblStyle);

        auto *spin = new QDoubleSpinBox(m_panel);
        spin->setObjectName(id);
        spin->setRange(double(lo), double(hi));
        spin->setDecimals(dec);
        spin->setSuffix(QString::fromLatin1(c.suffix));
        if (id == QLatin1String("ratio"))   spin->setSingleStep(0.1);
        if (id == QLatin1String("makeup"))  spin->setSingleStep(0.5);
        if (m_comp) spin->setValue(double(m_comp->parameterValue(id)));
        spin->setStyleSheet(spinStyle);

        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id](double v) {
            if (m_comp) m_comp->setParameterValue(id, float(v));
        });

        grid->addWidget(lab,  0, col);
        grid->addWidget(spin, 1, col);
        ++col;
    }
}

void CompressorDialog::syncPanel() {
    if (!m_comp || !m_panel) return;
    for (auto *spin : m_panel->findChildren<QDoubleSpinBox *>()) {
        const QString id = spin->objectName();
        if (id.isEmpty()) continue;
        QSignalBlocker sb(spin);
        spin->setValue(double(m_comp->parameterValue(id)));
    }
}

} // namespace quewi::ui
