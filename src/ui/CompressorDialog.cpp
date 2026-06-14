#include "ui/CompressorDialog.h"
#include "audio/effects/CompressorEffect.h"
#include "ui/LiveAudioScope.h"
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

void CompressorDialog::setScope(LiveAudioScope *scope) {
    if (m_scope) disconnect(m_scope, nullptr, this, nullptr);
    m_scope = scope;
    if (m_scope)
        connect(m_scope, &LiveAudioScope::updated, this,
                qOverload<>(&QWidget::update));
    update();
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

    const auto &tk = Theme::tokens();
    p.fillRect(rect(), tk.bgDeep);
    if (m_graphRect.isNull()) layoutGraph();

    // Float coord helpers — the curve must NOT snap to whole pixels; the
    // int-snapped polyline is what made the knee read as a jagged staircase.
    auto inToXf = [&](float dB) -> qreal {
        return m_graphRect.left()
             + qreal(dB - kInMin) / (kInMax - kInMin) * m_graphRect.width();
    };
    auto outToYf = [&](float dB) -> qreal {
        return m_graphRect.top()
             + qreal(kOutMax - dB) / (kOutMax - kOutMin) * m_graphRect.height();
    };

    // Graph background — slight vertical gradient.
    QLinearGradient grad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    grad.setColorAt(0.0, tk.bgPanel);
    grad.setColorAt(1.0, tk.bgDeep);
    p.fillRect(m_graphRect, grad);
    p.setPen(tk.outline);
    p.drawRect(m_graphRect.adjusted(0, 0, -1, -1));

    // ── dB grid (both axes share dB units) ──────────────────────────
    p.setFont(QFont(font().family(), 8));
    for (int dB = -60; dB <= 0; dB += 6) {
        const int x = inToX(float(dB));
        const bool major = (dB == 0 || dB == -60);
        p.setPen(major ? tk.outline : tk.divider);
        p.drawLine(x, m_graphRect.top(), x, m_graphRect.bottom());
        p.setPen(tk.ink40);
        p.drawText(QRect(x - 16, m_graphRect.bottom() + 3, 32, 14),
                   Qt::AlignCenter, QString::number(dB));
    }
    for (int dB = -60; dB <= 12; dB += 6) {
        const int y = outToY(float(dB));
        const bool major = (dB == 0);
        p.setPen(major ? tk.ink40 : tk.divider);
        p.drawLine(m_graphRect.left(), y, m_graphRect.right(), y);
        p.setPen(tk.ink40);
        p.drawText(QRect(m_graphRect.left() - 44, y - 7, 38, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   dB > 0 ? QStringLiteral("+%1").arg(dB) : QString::number(dB));
    }

    // Axis caption
    p.setPen(tk.ink60);
    p.setFont(QFont(font().family(), 8, QFont::DemiBold));
    p.drawText(QRect(m_graphRect.left(), m_graphRect.bottom() + 18, m_graphRect.width(), 14),
               Qt::AlignCenter, tr("INPUT  (dBFS)"));

    if (!m_comp) return;

    const float thr    = m_comp->thresholdDb();
    const float knee   = m_comp->kneeDb();
    const float makeup = m_comp->makeupDb();

    // Knee band — faint accent shade across [thr-knee/2, thr+knee/2] so the
    // soft-knee region is visible.
    if (knee > 0.1f) {
        QColor kb = tk.accent; kb.setAlpha(22);
        const qreal x0 = inToXf(std::max(kInMin, thr - knee * 0.5f));
        const qreal x1 = inToXf(std::min(kInMax, thr + knee * 0.5f));
        p.fillRect(QRectF(x0, m_graphRect.top(), x1 - x0, m_graphRect.height()), kb);
    }

    // ── 1:1 reference (no compression, no makeup) ───────────────────
    {
        QPen ref(tk.ink40); ref.setStyle(Qt::DashLine); ref.setWidthF(1.2);
        p.setPen(ref);
        p.drawLine(QPointF(inToXf(kInMin), outToYf(std::clamp(kInMin, kOutMin, kOutMax))),
                   QPointF(inToXf(kInMax), outToYf(std::clamp(kInMax, kOutMin, kOutMax))));
    }
    // Makeup reference (1:1 shifted up by makeup) so the output lift is visible.
    if (std::abs(makeup) > 0.05f) {
        QColor mc = tk.warn; mc.setAlpha(120);
        QPen mk(mc); mk.setStyle(Qt::DotLine); mk.setWidthF(1.1);
        p.setPen(mk);
        p.drawLine(QPointF(inToXf(kInMin), outToYf(std::clamp(kInMin + makeup, kOutMin, kOutMax))),
                   QPointF(inToXf(kInMax), outToYf(std::clamp(kInMax + makeup, kOutMin, kOutMax))));
    }

    // ── Transfer curve as a smooth float path — sub-pixel, densified
    //    through the knee where the slope bends (no int staircase). ───
    QPainterPath curvePath, fillPath;
    {
        const float kneeLo = thr - knee * 0.5f - 1.f;
        const float kneeHi = thr + knee * 0.5f + 1.f;
        bool first = true;
        float in = kInMin;
        while (true) {
            const float out = std::clamp(m_comp->transferOutputDb(in), kOutMin, kOutMax);
            const QPointF pt(inToXf(in), outToYf(out));
            if (first) { curvePath.moveTo(pt);
                         fillPath.moveTo(pt.x(), m_graphRect.bottom());
                         fillPath.lineTo(pt); first = false; }
            else       { curvePath.lineTo(pt); fillPath.lineTo(pt); }
            if (in >= kInMax) break;
            in = std::min(kInMax, in + ((in > kneeLo && in < kneeHi) ? 0.25f : 0.75f));
        }
        fillPath.lineTo(inToXf(kInMax), m_graphRect.bottom());
        fillPath.closeSubpath();
    }
    QLinearGradient fg(0, m_graphRect.top(), 0, m_graphRect.bottom());
    QColor fgTop = tk.info; fgTop.setAlpha(50);
    QColor fgBot = tk.info; fgBot.setAlpha(0);
    fg.setColorAt(0.0, fgTop); fg.setColorAt(1.0, fgBot);
    p.fillPath(fillPath, fg);

    // ── Gain-reduction WEDGE — the gap between the (de-makeup'd) curve and
    //    the 1:1 line, which only opens above threshold. The single clearest
    //    way to SEE how much the compressor is pulling the signal down. ──
    {
        QPainterPath wedge;
        bool w0 = true;
        for (float x = kInMin; x <= kInMax; x += 0.75f) {
            const float out = std::clamp(m_comp->transferOutputDb(x) - makeup, kOutMin, kOutMax);
            const QPointF pt(inToXf(x), outToYf(out));
            if (w0) { wedge.moveTo(pt); w0 = false; } else wedge.lineTo(pt);
        }
        for (float x = kInMax; x >= kInMin; x -= 0.75f)
            wedge.lineTo(QPointF(inToXf(x), outToYf(std::clamp(x, kOutMin, kOutMax))));
        wedge.closeSubpath();
        QColor wc = tk.err; wc.setAlpha(40);
        p.fillPath(wedge, wc);
    }

    QPen curvePen(tk.info); curvePen.setWidthF(2.2);
    p.setPen(curvePen); p.setBrush(Qt::NoBrush);
    p.drawPath(curvePath);

    // Threshold guide line
    {
        const int tx = inToX(thr);
        QColor tc = tk.accent; tc.setAlpha(160);
        QPen tp(tc); tp.setStyle(Qt::DashLine);
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
        p.setPen(QPen(tk.bgDeep, 2));
        p.drawEllipse(c, kHandleRadius, kHandleRadius);
    };
    drawHandle(thresholdHandlePos(), tk.accent,
               m_hoverHandle == 1 || m_drag == Drag::Threshold);
    drawHandle(ratioHandlePos(), tk.running,
               m_hoverHandle == 2 || m_drag == Drag::Ratio);

    // ── Live program level riding the curve (peak + RMS dots) ───────
    const bool liveScope = (m_scope && m_scope->active());
    if (liveScope) {
        const float lvl    = std::clamp(m_scope->peakDb(), kInMin, kInMax);
        const float outLvl = std::clamp(m_comp->transferOutputDb(lvl), kOutMin, kOutMax);
        const qreal lx = inToXf(lvl);
        QColor gl = tk.running; gl.setAlpha(120);
        p.setPen(QPen(gl, 1, Qt::DashLine));
        p.drawLine(QPointF(lx, m_graphRect.top()), QPointF(lx, m_graphRect.bottom()));
        // RMS dot (smaller, behind the peak dot).
        const float rms  = std::clamp(m_scope->rmsDb(), kInMin, kInMax);
        const float rOut = std::clamp(m_comp->transferOutputDb(rms), kOutMin, kOutMax);
        QColor rc = tk.running; rc.setAlpha(150);
        p.setBrush(rc); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(inToXf(rms), outToYf(rOut)), 3.5, 3.5);
        // Peak dot.
        p.setBrush(tk.running); p.setPen(QPen(tk.bgDeep, 2));
        p.drawEllipse(QPointF(lx, outToYf(outLvl)), 5, 5);
    }

    // ── Gain-reduction meter (driven by the real envelope so attack/release
    //    ballistics are visible — the static curve value can't show motion) ──
    const int mx = m_graphRect.right() + 18;
    const QRect meter(mx, m_graphRect.top(), kMeterWidth, m_graphRect.height());
    p.fillRect(meter, tk.bgDeep);
    p.setPen(tk.outline);
    p.drawRect(meter.adjusted(0, 0, -1, -1));
    const float gr = std::clamp(-m_comp->currentGainReductionDb(), 0.f, kMeterRange);
    const int barH = int(gr / kMeterRange * meter.height());
    if (barH > 0) {
        QLinearGradient mg(0, meter.top(), 0, meter.bottom());
        mg.setColorAt(0.0, tk.err);
        mg.setColorAt(0.5, tk.warn);
        mg.setColorAt(1.0, tk.running);
        p.fillRect(QRect(meter.left() + 1, meter.top() + 1,
                         meter.width() - 2, barH), mg);
    }
    // dB ticks down the meter's right edge.
    p.setPen(tk.ink40);
    for (int d : {0, 3, 6, 12, 18, 24}) {
        const int ty = meter.top() + int(float(d) / kMeterRange * meter.height());
        p.drawLine(meter.right(), ty, meter.right() + 3, ty);
    }
    p.setPen(tk.ink60);
    p.setFont(QFont(font().family(), 7, QFont::DemiBold));
    p.drawText(QRect(mx - 6, meter.bottom() + 4, kMeterWidth + 30, 12),
               Qt::AlignLeft, tr("GR"));

    // ── IN / OUT / GR readout — the numbers that make compression legible ──
    {
        const float inDb  = liveScope ? m_scope->peakDb() : -99.f;
        const float outDb = liveScope
            ? m_comp->transferOutputDb(std::clamp(inDb, kInMin, kInMax)) : -99.f;
        auto fmt = [](float v) {
            return v <= -90.f ? QStringLiteral("––") : QString::number(v, 'f', 1);
        };
        const QString line = QStringLiteral("IN %1     OUT %2     GR -%3")
            .arg(fmt(inDb), fmt(outDb)).arg(gr, 0, 'f', 1);
        p.setFont(QFont(QStringLiteral("Space Grotesk"), 9, QFont::DemiBold));
        p.setPen(liveScope ? tk.ink100 : tk.ink40);
        p.drawText(QRect(m_graphRect.left() + 8, m_graphRect.top() + 5,
                         m_graphRect.width() - 16, 16),
                   Qt::AlignLeft, line);
    }

    // ── Cursor readout ──────────────────────────────────────────────
    if (m_graphRect.contains(m_cursor)) {
        QColor cl = tk.ink60; cl.setAlpha(140);
        p.setPen(QPen(cl, 1, Qt::DashLine));
        p.drawLine(m_cursor.x(), m_graphRect.top(), m_cursor.x(), m_graphRect.bottom());
        const float in  = xToIn(m_cursor.x());
        const float out = m_comp->transferOutputDb(in);
        const QString readout =
            QStringLiteral("in %1   out %2").arg(in, 0, 'f', 1).arg(out, 0, 'f', 1);
        p.setFont(QFont(QStringLiteral("Space Grotesk"), 9, QFont::Medium));
        const QFontMetrics fm(p.font());
        const QSize sz = fm.size(0, readout) + QSize(14, 8);
        const int rx = std::min(m_cursor.x() + 12, m_graphRect.right() - sz.width());
        const int ry = m_graphRect.top() + 24;   // sits below the IN/OUT/GR line
        const QRect rrect(rx, ry, sz.width(), sz.height());
        QColor box = tk.bgDeep; box.setAlpha(220);
        p.fillRect(rrect, box);
        p.setPen(tk.outline);
        p.drawRect(rrect.adjusted(0, 0, -1, -1));
        p.setPen(tk.ink100);
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
