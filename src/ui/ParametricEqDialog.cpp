#include "ui/ParametricEqDialog.h"
#include "audio/effects/EqEffect.h"
#include "ui/LiveAudioScope.h"
#include "ui/Theme.h"

#include <QAction>
#include <QUndoCommand>
#include <QUndoStack>

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
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::ui {

QColor ParametricEqDialog::bandColor(int i) {
    // Cool → warm spectrum, six steps, drawn from the theme's restrained
    // state pastels so band identity stays distinct on the dark graph
    // without importing colours from outside the token set.
    const auto &tk = Theme::tokens();
    switch (i) {
        case 0: return tk.info;        // 1 — blue (low shelf)
        case 1: return tk.loaded;      // 2 — dusty blue
        case 2: return tk.running;     // 3 — mossy green
        case 3: return tk.warnBright;  // 4 — bright yellow
        case 4: return tk.warn;        // 5 — amber
        case 5: return tk.err;         // 6 — terracotta (high shelf)
        default: return tk.ink100;
    }
}

ParametricEqDialog::ParametricEqDialog(audio::EqEffect *eq, QWidget *parent)
    : QDialog(parent), m_eq(eq)
{
    setWindowTitle(tr("Parametric EQ"));
    setMinimumSize(980, 660);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_panel = new QWidget(this);
    // Tall enough for the full band column (swatch + type + freq + gain + Q)
    // without the controls overlapping or the bottom row clipping.
    m_panel->setFixedHeight(238);
    m_panel->setStyleSheet(QStringLiteral("background:%1;")
                              .arg(Theme::tokens().bgDeep.name()));

    vl->addStretch(1); // graph area (paintEvent draws here)
    vl->addWidget(m_panel);

    rebuildBandPanel();

    // Undo/redo for band edits — Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z.
    m_undo = new QUndoStack(this);
    captureState(m_prevState);
    auto *undoAct = new QAction(tr("Undo"), this);
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, [this] { if (m_undo) m_undo->undo(); });
    addAction(undoAct);
    auto *redoAct = new QAction(tr("Redo"), this);
    redoAct->setShortcuts(QList<QKeySequence>{
        QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z")) });
    connect(redoAct, &QAction::triggered, this, [this] { if (m_undo) m_undo->redo(); });
    addAction(redoAct);

    // Coalesced repaint — at most one update() per ~16ms frame.
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(16);
    connect(m_repaintTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    if (m_eq) {
        connect(m_eq, &audio::AudioEffect::parameterChanged, this,
                [this](const QString &, float) {
            // setBand() emits three parameterChanged signals per change, so this
            // slot fires 3× per drag move. updatePanel() does ~30 findChild()
            // scans; running it 3× per move (with the heavy repaint) saturates
            // the GUI thread and the editor preview's audio sink underruns.
            // During a drag the panel is resynced once on mouse-release, so skip
            // it here; otherwise (spin-box / type edits) keep it live. Repaints
            // are coalesced to one per frame either way.
            if (m_dragBand < 0) updatePanel();
            scheduleRepaint();
        });
    }
}

namespace {
// Coarse-grained EQ undo: stores the full 6-band state before and after a
// gesture and restores it wholesale. Simpler and more robust than per-control
// commands, and an EQ has few enough bands that it's cheap.
class EqStateCommand : public QUndoCommand {
public:
    EqStateCommand(ParametricEqDialog *dlg,
                   QList<ParametricEqDialog::EqBandState> before,
                   QList<ParametricEqDialog::EqBandState> after)
        : m_dlg(dlg), m_before(std::move(before)), m_after(std::move(after)) {
        setText(QStringLiteral("EQ change"));
    }
    void undo() override { if (m_dlg) m_dlg->applyState(m_before); }
    void redo() override { if (m_dlg) m_dlg->applyState(m_after); }
private:
    QPointer<ParametricEqDialog> m_dlg;
    QList<ParametricEqDialog::EqBandState> m_before, m_after;
};

bool eqStatesEqual(const QList<ParametricEqDialog::EqBandState> &a,
                   const QList<ParametricEqDialog::EqBandState> &b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        const auto &x = a[i]; const auto &y = b[i];
        if (x.freq != y.freq || x.gainDb != y.gainDb || x.Q != y.Q
            || x.type != y.type || x.enabled != y.enabled)
            return false;
    }
    return true;
}
} // namespace

void ParametricEqDialog::captureState(QList<EqBandState> &out) const {
    out.clear();
    if (!m_eq) return;
    for (int i = 0; i < audio::EqEffect::kNumBands; ++i) {
        const auto sn = m_eq->bandSnapshot(i);
        out.push_back({sn.freq, sn.gainDb, sn.Q, int(sn.type), sn.enabled});
    }
}

void ParametricEqDialog::applyState(const QList<EqBandState> &st) {
    if (!m_eq) return;
    for (int i = 0; i < st.size() && i < audio::EqEffect::kNumBands; ++i) {
        const auto &s = st[i];
        m_eq->setBandType(i, audio::EqEffect::FilterType(s.type));
        m_eq->setBand(i, s.freq, s.gainDb, s.Q);
        m_eq->setBandEnabled(i, s.enabled);
    }
    m_prevState = st;   // stay in sync so the next maybeCommit() is a no-op
    updatePanel();
    update();
}

void ParametricEqDialog::maybeCommit() {
    if (!m_eq || !m_undo) return;
    QList<EqBandState> cur;
    captureState(cur);
    if (eqStatesEqual(cur, m_prevState)) return;
    m_undo->push(new EqStateCommand(this, m_prevState, cur));
    m_prevState = cur;
}

void ParametricEqDialog::setScope(LiveAudioScope *scope) {
    if (m_scope) disconnect(m_scope, nullptr, this, nullptr);
    m_scope = scope;
    if (m_scope)
        connect(m_scope, &LiveAudioScope::updated, this,
                qOverload<>(&QWidget::update));
    update();
}

void ParametricEqDialog::resizeEvent(QResizeEvent *) {
    layoutGraph();
}

void ParametricEqDialog::layoutGraph() {
    m_graphRect = QRect(0, 0, width(), height() - m_panel->height());
    m_graphRect.adjust(48, 18, -18, -22); // axis margins
    rebuildCurveFreqs();
}

void ParametricEqDialog::rebuildCurveFreqs() {
    const int n = std::max(0, m_graphRect.width());
    m_curveFreqs.resize(n);
    for (int i = 0; i < n; ++i)
        m_curveFreqs[i] = xToFreq(m_graphRect.left() + i);
}

void ParametricEqDialog::scheduleRepaint() {
    if (m_repaintTimer && !m_repaintTimer->isActive()) m_repaintTimer->start();
    else if (!m_repaintTimer) update();
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

    const auto &tk = Theme::tokens();

    // Window background.
    p.fillRect(rect(), tk.bgDeep);
    if (m_graphRect.isNull()) layoutGraph();

    // Graph background — slight vertical gradient.
    QLinearGradient grad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    grad.setColorAt(0.0, tk.bgPanel);
    grad.setColorAt(1.0, tk.bgDeep);
    p.fillRect(m_graphRect, grad);

    // Frame
    p.setPen(tk.outline);
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
        p.setPen(major ? tk.outline : tk.divider);
        p.drawLine(x, m_graphRect.top(), x, m_graphRect.bottom());
    }
    // dB grid
    for (int dB = int(kGainMin); dB <= int(kGainMax); dB += 3) {
        const int y = gainToY(float(dB));
        const bool major = (dB == 0);
        const bool half  = (dB % 6 == 0);
        QColor c = major ? tk.ink40
                  : half  ? tk.outline
                          : tk.divider;
        p.setPen(c);
        p.drawLine(m_graphRect.left(), y, m_graphRect.right(), y);
    }

    // ── Axis labels ─────────────────────────────────────────────────
    p.setPen(tk.ink40);
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

    // ── Live spectrum analyzer (behind the EQ curves) ───────────────
    if (m_scope && m_scope->active()) {
        const auto &mag = m_scope->magnitudes();
        const int bins = int(mag.size());
        if (bins > 1) {
            const float nyq = float(m_scope->sampleRate()) * 0.5f;
            // Map analyzer level (dBFS, -100..0) across the graph height.
            auto specY = [&](float db) {
                const float t = std::clamp((db + 100.f) / 100.f, 0.f, 1.f);
                return float(m_graphRect.bottom()) - t * float(m_graphRect.height());
            };
            QVector<QPointF> pts;
            pts.reserve(m_graphRect.width() / 2 + 2);
            for (int px = 0; px <= m_graphRect.width(); px += 2) {
                const int x = m_graphRect.left() + px;
                const float f = xToFreq(x);
                int bin = std::clamp(int(f / nyq * float(bins - 1)), 1, bins - 1);
                const float m = mag[size_t(bin)];
                const float db = (m > 1e-9f) ? 20.f * std::log10(m) : -100.f;
                pts.append(QPointF(double(x), double(specY(db))));
            }
            if (pts.size() >= 2) {
                QPainterPath area;
                area.moveTo(pts.front().x(), m_graphRect.bottom());
                for (const auto &pt : pts) area.lineTo(pt);
                area.lineTo(pts.back().x(), m_graphRect.bottom());
                area.closeSubpath();
                // DM7-style level colouring: loud content (toward the top of
                // the graph) reads red, mids amber/yellow, quiet content green.
                // The gradient is keyed to graph height, so a peak that climbs
                // into the red zone shows red at its tip.
                QColor gRed    = tk.errBright;  gRed.setAlpha(195);
                QColor gOrange = tk.warn;       gOrange.setAlpha(170);
                QColor gYellow = tk.warnBright; gYellow.setAlpha(135);
                QColor gGreen  = tk.running;    gGreen.setAlpha(100);
                QColor gFloor  = tk.running;    gFloor.setAlpha(0);
                QLinearGradient g(0, m_graphRect.top(), 0, m_graphRect.bottom());
                g.setColorAt(0.00, gRed);    // red — peaking
                g.setColorAt(0.22, gOrange); // orange
                g.setColorAt(0.45, gYellow); // yellow
                g.setColorAt(0.72, gGreen);  // green
                g.setColorAt(1.00, gFloor);  // fade at the floor
                p.fillPath(area, g);
                QColor spectrumLine = tk.ink100; spectrumLine.setAlpha(60);
                p.setPen(QPen(spectrumLine, 1.0));
                p.drawPolyline(pts.constData(), int(pts.size()));
            }
        }
    }

    if (!m_eq) return;

    // ── Per-band ghost curves ───────────────────────────────────────
    // Render each band's individual contribution as a thin coloured
    // curve, alpha-faded if the band is bypassed. Lets the operator see
    // exactly which band is doing what.
    const int yZero = gainToY(0.f);
    const int nPts = m_graphRect.width();
    // Index the precomputed per-pixel frequency map when it matches the current
    // width; fall back to xToFreq() if it's momentarily stale (pre-resize).
    const bool haveCache = (m_curveFreqs.size() == nPts);
    for (int b = 0; b < audio::EqEffect::kNumBands; ++b) {
        const auto sn = m_eq->bandSnapshot(b);
        // A peaking/shelf band sitting at ~0 dB is flat everywhere — skip its
        // ghost curve before paying for nPts biquad-magnitude evaluations.
        // Low/high-pass filters always shape the signal, so never skip those.
        if ((sn.type == audio::EqEffect::Peaking
             || sn.type == audio::EqEffect::LowShelf
             || sn.type == audio::EqEffect::HighShelf)
            && std::abs(sn.gainDb) < 0.05f)
            continue;
        QVector<QPointF> bandCurve;
        bandCurve.reserve(nPts);
        bool nonTrivial = false;
        for (int i = 0; i < nPts; ++i) {
            const int x = m_graphRect.left() + i;
            const float f  = haveCache ? m_curveFreqs[i] : xToFreq(x);
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
        const float f  = haveCache ? m_curveFreqs[i] : xToFreq(x);
        const float dB = m_eq->responseDb(f);
        curve.append(QPointF(x, gainToY(std::clamp(dB, kGainMin, kGainMax))));
    }
    QPainterPath fill;
    fill.moveTo(curve.front().x(), yZero);
    for (auto &pt : curve) fill.lineTo(pt);
    fill.lineTo(curve.back().x(), yZero);
    fill.closeSubpath();

    // Composite response — info blue, matching the CompressorDialog's
    // transfer curve so the two audio graphs read as the same instrument.
    QColor fillHi = tk.info; fillHi.setAlpha(70);
    QColor fillMd = tk.info; fillMd.setAlpha(25);
    QColor fillLo = tk.info; fillLo.setAlpha(0);
    QLinearGradient fillGrad(0, m_graphRect.top(), 0, m_graphRect.bottom());
    fillGrad.setColorAt(0.0, fillHi);
    fillGrad.setColorAt(0.5, fillMd);
    fillGrad.setColorAt(1.0, fillLo);
    p.fillPath(fill, fillGrad);

    QPen curvePen(tk.info); curvePen.setWidthF(2.0);
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
        p.setPen(QPen(tk.bgDeep, 2));
        p.drawEllipse(c, kHandleRadius, kHandleRadius);

        p.setPen(tk.inkOnAccent);
        p.setFont(QFont(font().family(), 9, QFont::Bold));
        p.drawText(QRect(c.x() - kHandleRadius, c.y() - kHandleRadius,
                         kHandleRadius * 2, kHandleRadius * 2),
                   Qt::AlignCenter, QString::number(b + 1));
    }

    // ── Cursor crosshair + readout ─────────────────────────────────
    if (m_graphRect.contains(m_cursor)) {
        QColor cross = tk.ink60; cross.setAlpha(160);
        p.setPen(QPen(cross, 1, Qt::DashLine));
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
        QColor box = tk.bgDeep; box.setAlpha(220);
        p.fillRect(rrect, box);
        p.setPen(tk.outline);
        p.drawRect(rrect.adjusted(0,0,-1,-1));
        p.setPen(tk.ink100);
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
    scheduleRepaint();
}

void ParametricEqDialog::mouseReleaseEvent(QMouseEvent *) {
    const bool wasDragging = (m_dragBand >= 0);
    m_dragBand = -1;
    if (wasDragging) {
        updatePanel();  // resync spin boxes to the final drag values (gated above)
        maybeCommit();  // one undo step per drag
    }
}

void ParametricEqDialog::mouseDoubleClickEvent(QMouseEvent *e) {
    if (!m_eq) return;
    const int b = hitTestBand(e->pos());
    if (b < 0) return;
    const auto sn = m_eq->bandSnapshot(b);
    // Reset the band to flat at the same frequency.
    m_eq->setBand(b, sn.freq, 0.f, 0.707f);
    maybeCommit();
}

void ParametricEqDialog::wheelEvent(QWheelEvent *e) {
    if (!m_eq) return;
    const int b = hitTestBand(e->position().toPoint());
    if (b < 0) return;
    const auto sn = m_eq->bandSnapshot(b);
    const float factor = (e->angleDelta().y() > 0) ? 1.15f : 1.f / 1.15f;
    const float newQ = std::clamp(sn.Q * factor, 0.1f, 10.f);
    m_eq->setBand(b, sn.freq, sn.gainDb, newQ);
    maybeCommit();
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

    const auto &tk = Theme::tokens();
    const QString lblStyle = QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.10em;")
        .arg(tk.ink40.name());
    const QString spinStyle = QStringLiteral(
        "QDoubleSpinBox, QComboBox { background:%1; color:%2;"
        "  border:1px solid %3; padding:2px 4px; min-height:20px; }"
        "QDoubleSpinBox:focus, QComboBox:focus { border:1px solid %4; }")
        .arg(tk.bgInteractive.name(),
             tk.ink100.name(),
             tk.outline.name(),
             tk.outlineFocus.name());

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
            "color:%1; font-size:11px; font-weight:700; letter-spacing:0.10em;")
            .arg(tk.ink100.name()));

        auto *enableBox = new QCheckBox(header);
        enableBox->setObjectName(QStringLiteral("on%1").arg(b));
        enableBox->setChecked(sn.enabled);
        enableBox->setToolTip(tr("Bypass this band"));
        connect(enableBox, &QCheckBox::toggled, this, [this, b](bool on) {
            if (m_eq) m_eq->setBandEnabled(b, on);
            update();
            maybeCommit();
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
            maybeCommit();
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
            maybeCommit();
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
