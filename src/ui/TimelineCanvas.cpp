#include "ui/TimelineCanvas.h"
#include "ui/SpectrogramImage.h"
#include "ui/Theme.h"
#include "audio/AudioFile.h"

#include <QContextMenuEvent>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace quewi::ui {

TimelineCanvas::TimelineCanvas(audio::AudioEditorModel *model, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    if (model) {
        connect(model, &audio::AudioEditorModel::tracksChanged,  this, [this]{ updateScrollBars(); update(); });
        connect(model, &audio::AudioEditorModel::regionMoved,    this, [this]{ update(); });
    }
}

TimelineCanvas::~TimelineCanvas() = default;

void TimelineCanvas::setViewMode(ViewMode m) {
    if (m_viewMode == m) return;
    m_viewMode = m;
    update();
}

void TimelineCanvas::clearSpectrogramCache() {
    m_specImages.clear();
    m_specFrames.clear();
    // In-flight builds are left to finish; their results are dropped because
    // the entry is re-checked against the live frame count on completion.
}

void TimelineCanvas::ensureSpectrogram(const std::shared_ptr<audio::AudioFile> &file) {
    if (!file || file->state() != audio::AudioFile::State::Loaded) return;
    const audio::AudioFile *key = file.get();
    const qint64 fc = file->frameCount();
    if (m_specImages.contains(key) && m_specFrames.value(key) == fc) return; // fresh
    if (m_specBuilding.contains(key)) return;                                // building

    auto snap = file->snapshot();
    if (!snap) return;

    m_specBuilding.insert(key);
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key, fc] {
        m_specImages.insert(key, watcher->result());
        m_specFrames.insert(key, fc);
        m_specBuilding.remove(key);
        watcher->deleteLater();
        if (m_viewMode == ViewMode::Spectrogram) update();
    });
    watcher->setFuture(QtConcurrent::run([snap] {
        return spectro::buildFullFile(snap);
    }));
}

// ── Geometry ──────────────────────────────────────────────────────────────────

int TimelineCanvas::trackY(int idx) const {
    return tracksTop() + idx * m_trackHeight - m_scrollY;
}

int TimelineCanvas::contentHeight() const {
    if (!m_model) return tracksTop();
    return tracksTop() + m_model->trackCount() * m_trackHeight;
}

double TimelineCanvas::framesToX(qint64 frames) const {
    return kHeaderWidth + double(frames) / m_framesPerPixel - m_scrollX;
}

qint64 TimelineCanvas::xToFrames(int x) const {
    return qint64((double(x - kHeaderWidth) + m_scrollX) * m_framesPerPixel);
}

int TimelineCanvas::trackAtY(int y) const {
    if (y < tracksTop()) return -1;
    int idx = (y - tracksTop() + m_scrollY) / m_trackHeight;
    if (idx < 0 || !m_model || idx >= m_model->trackCount()) return -1;
    return idx;
}

void TimelineCanvas::setScrollBars(QScrollBar *hbar, QScrollBar *vbar) {
    m_hbar = hbar; m_vbar = vbar;
    if (hbar) connect(hbar, &QScrollBar::valueChanged, this, [this](int v){ m_scrollX = v; update(); });
    if (vbar) connect(vbar, &QScrollBar::valueChanged, this, [this](int v){ m_scrollY = v; update(); });
    updateScrollBars();
}

void TimelineCanvas::updateScrollBars() {
    if (!m_model) return;
    qint64 totalFrames = std::max(m_model->totalDurationSamples(), qint64(m_model->sampleRate() * 10));
    int    totalPx     = int(double(totalFrames) / m_framesPerPixel) + kHeaderWidth;
    int    viewPx      = std::max(1, width() - kHeaderWidth);

    if (m_hbar) {
        m_hbar->setRange(0, std::max(0, totalPx - viewPx));
        m_hbar->setPageStep(viewPx);
        m_hbar->setSingleStep(viewPx / 10);
    }
    int contentH = contentHeight();
    if (m_vbar) {
        m_vbar->setRange(0, std::max(0, contentH - height()));
        m_vbar->setPageStep(height() - tracksTop());
        m_vbar->setSingleStep(m_trackHeight);
    }
}

void TimelineCanvas::setFramesPerPixel(double fpp) {
    m_framesPerPixel = std::clamp(fpp, 1.0, 48000.0 * 60.0);
    updateScrollBars();
    update();
}

void TimelineCanvas::setPlayheadFrame(qint64 f) {
    m_playheadFrame = f; update();
}

void TimelineCanvas::setEditCursorFrame(qint64 f) {
    m_editCursorFrame = std::max<qint64>(0, f);
    update();
}

// ── Lighting triggers ─────────────────────────────────────────────────────────

void TimelineCanvas::setTriggers(const audio::LightTriggers &t, double sampleRate) {
    m_triggers = t;
    if (sampleRate > 0.0) m_triggerRate = sampleRate;
    // The trigger being dragged went away (undo, remote): drop the drag.
    if (m_tdrag.mode != TriggerDrag::None && !m_tdrag.id.isNull()
        && std::none_of(m_triggers.begin(), m_triggers.end(),
                        [this](const audio::LightTrigger &x) { return x.id == m_tdrag.id; }))
        m_tdrag = TriggerDrag{};
    update();
}

void TimelineCanvas::setSelectedTrigger(const QUuid &id) {
    setSelectedTriggers(id.isNull() ? QList<QUuid>{} : QList<QUuid>{id});
}

void TimelineCanvas::setSelectedTriggers(const QList<QUuid> &ids) {
    QSet<QUuid> next(ids.begin(), ids.end());
    if (next == m_selTriggers) return;
    m_selTriggers = std::move(next);
    update();
}

void TimelineCanvas::flashTrigger(const QUuid &id) {
    m_flashTrigger = id;
    update();
    QTimer::singleShot(450, this, [this, id] {
        if (m_flashTrigger != id) return;
        m_flashTrigger = QUuid();
        update();
    });
}

void TimelineCanvas::setBeatGrid(const audio::BeatGrid &g) {
    if (g == m_grid) return;
    m_grid = g;
    update();
}

double TimelineCanvas::snapSeconds(double s, bool noSnap) const {
    if (!m_snap || noSnap || !m_grid.isSet()) return s;
    return m_grid.snap(s);
}

void TimelineCanvas::drawBeatLines(QPainter &p, int top, int h, bool lane) {
    if (!m_grid.isSet() || h <= 0) return;
    const double pxPerBeat = m_grid.beatLength() * m_triggerRate / m_framesPerPixel;
    const int bpb = std::max(1, m_grid.beatsPerBar);
    const double pxPerBar = pxPerBeat * bpb;
    const bool beats = lane && pxPerBeat >= 4.0;    // else bars only
    if (pxPerBar < (lane ? 4.0 : 8.0)) return;      // too dense to mean anything
    const long long step = beats ? 1 : bpb;

    const double s0 = xToSeconds(kHeaderWidth), s1 = xToSeconds(width());
    auto floorTo = [](long long v, long long n) { return (v >= 0 ? v / n : -((-v + n - 1) / n)) * n; };
    const long long b0 = floorTo(static_cast<long long>(std::floor(m_grid.beatAt(s0))) - 1, step);
    const long long b1 = static_cast<long long>(std::ceil(m_grid.beatAt(s1))) + 1;
    if ((b1 - b0) / step > 20000) return;

    // Neutral ink, kept faint: the amber accent belongs to the markers.
    const auto &tk = Theme::tokens();
    QColor barCol = tk.ink100, beatCol = tk.ink100;
    barCol.setAlpha(lane ? 70 : 20);
    beatCol.setAlpha(26);
    QFont nf = font();
    nf.setPointSizeF(6.5);
    const bool numbers = lane && pxPerBar >= 28.0;
    if (numbers) p.setFont(nf);

    for (long long b = b0; b <= b1; b += step) {
        const double t = m_grid.timeOfBeat(double(b));
        if (t < 0.0) continue;
        const int x = int(secondsToX(t));
        if (x < kHeaderWidth || x >= width()) continue;
        const bool bar = m_grid.isBarLine(b);
        if (!lane && !bar) continue;
        p.fillRect(x, top, 1, h, bar ? barCol : beatCol);
        if (numbers && bar && b >= 0) {
            p.setPen(tk.ink40);
            p.drawText(QRect(x + 2, top, 40, h), Qt::AlignLeft | Qt::AlignTop,
                       QString::number(b / bpb + 1));
        }
    }
}

double TimelineCanvas::secondsToX(double s) const {
    return kHeaderWidth + s * m_triggerRate / m_framesPerPixel - m_scrollX;
}

double TimelineCanvas::xToSeconds(int x) const {
    return std::max(0.0, (double(x - kHeaderWidth) + m_scrollX) * m_framesPerPixel / m_triggerRate);
}

void TimelineCanvas::shownSpan(const audio::LightTrigger &t, double &start, double &end) const {
    if (m_tdrag.moved && m_tdrag.mode == TriggerDrag::MoveMany && m_selTriggers.contains(t.id)) {
        const double d = m_tdrag.curStart - m_tdrag.origStart;
        start = t.start + d;
        end   = t.isRange() ? t.end + d : -1.0;
    } else if (m_tdrag.moved && t.id == m_tdrag.id
               && m_tdrag.mode != TriggerDrag::Span && m_tdrag.mode != TriggerDrag::Create) {
        start = m_tdrag.curStart;
        end   = m_tdrag.curEnd;
    } else {
        start = t.start;
        end   = t.isRange() ? t.end : -1.0;
    }
}

int TimelineCanvas::triggerAt(int x, TriggerPart *part) const {
    constexpr int kPointHit = 6;
    // Points sit on top of ranges, so they win.
    for (int i = int(m_triggers.size()) - 1; i >= 0; --i) {
        const auto &t = m_triggers[size_t(i)];
        if (t.isRange()) continue;
        if (std::abs(secondsToX(t.start) - x) <= kPointHit) {
            if (part) *part = TriggerPart::Body;
            return i;
        }
    }
    for (int i = int(m_triggers.size()) - 1; i >= 0; --i) {
        const auto &t = m_triggers[size_t(i)];
        if (!t.isRange()) continue;
        const double x1 = secondsToX(t.start), x2 = secondsToX(t.end);
        if (x < x1 - kEdgeTolerance || x > x2 + kEdgeTolerance) continue;
        // A range too narrow for three zones is all body: moving beats
        // resizing something you can barely see.
        TriggerPart pt = TriggerPart::Body;
        if (x2 - x1 > 3 * kEdgeTolerance) {
            if (std::abs(x - x1) <= kEdgeTolerance)      pt = TriggerPart::StartEdge;
            else if (std::abs(x - x2) <= kEdgeTolerance) pt = TriggerPart::EndEdge;
        }
        if (part) *part = pt;
        return i;
    }
    return -1;
}

void TimelineCanvas::drawTriggerGuides(QPainter &p) {
    if (m_triggers.empty()) return;
    const auto &tk = Theme::tokens();
    p.save();
    p.setClipRect(kHeaderWidth, tracksTop(), width() - kHeaderWidth, height() - tracksTop());
    for (const auto &t : m_triggers) {
        double s = 0, e = 0;
        shownSpan(t, s, e);
        const bool sel = m_selTriggers.contains(t.id) || t.id == m_flashTrigger;
        QColor line = tk.accent;
        line.setAlpha(!t.enabled ? 22 : sel ? 120 : 55);
        const int x1 = int(secondsToX(s));
        if (e > s) {
            // A range also tints the tracks very faintly, so the section reads.
            const int x2 = int(secondsToX(e));
            if (x2 < kHeaderWidth || x1 > width()) continue;
            QColor wash = tk.accent;
            wash.setAlpha(t.enabled ? (sel ? 22 : 12) : 6);
            p.fillRect(QRect(x1, tracksTop(), x2 - x1, height() - tracksTop()), wash);
            p.fillRect(x2, tracksTop(), 1, height() - tracksTop(), line);
        }
        p.fillRect(x1, tracksTop(), 1, height() - tracksTop(), line);
    }
    p.restore();
}

void TimelineCanvas::drawTriggerLane(QPainter &p) {
    const auto &tk = Theme::tokens();
    const int top = kRulerHeight, h = kMarkerLaneHeight;
    p.fillRect(0, top, width(), h, tk.bgRowAlt);
    p.fillRect(0, top + h - 1, width(), 1, tk.divider);

    // Header cell, matching the track headers below it.
    p.fillRect(0, top, kHeaderWidth, h - 1, tk.bgInteractive);
    p.fillRect(kHeaderWidth - 1, top, 1, h, tk.bgDeep);
    QFont cap = font();
    cap.setPointSizeF(7.5);
    cap.setBold(true);
    cap.setLetterSpacing(QFont::PercentageSpacing, 115);
    p.setFont(cap);
    p.setPen(tk.ink40);
    p.drawText(QRect(8, top, kHeaderWidth - 12, h - 1), Qt::AlignLeft | Qt::AlignVCenter,
               tr("LIGHTING"));

    p.save();
    p.setClipRect(kHeaderWidth, top, width() - kHeaderWidth, h - 1);
    drawBeatLines(p, top, h - 1, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont nf = font();
    nf.setPointSizeF(8.0);
    p.setFont(nf);
    const QFontMetrics fm(nf);
    const double midY = top + (h - 1) / 2.0;

    // Ranges first, under the points.
    for (const auto &t : m_triggers) {
        double s = 0, e = 0;
        shownSpan(t, s, e);
        if (e <= s) continue;
        const double x1 = secondsToX(s), x2 = secondsToX(e);
        if (x2 < kHeaderWidth || x1 > width()) continue;
        const bool sel = m_selTriggers.contains(t.id);
        const bool hot = sel || t.id == m_flashTrigger;
        QColor fill = tk.accent;
        fill.setAlpha(!t.enabled ? 26 : hot ? 125 : 70);
        const QColor edge = !t.enabled ? tk.ink40 : sel ? tk.accentHover : tk.accent;
        const QRectF r(x1 + 0.5, top + 3.5, std::max(2.0, x2 - x1 - 1.0), h - 8.0);
        p.setPen(QPen(edge, sel ? 1.5 : 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(r, 3, 3);
        if (!t.name.isEmpty() && r.width() > 24) {
            p.setPen(t.enabled ? tk.ink100 : tk.ink40);
            p.drawText(r.adjusted(5, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(t.name, Qt::ElideRight, int(r.width()) - 9));
        }
    }

    // Points: a small diamond flag, name beside it.
    for (const auto &t : m_triggers) {
        double s = 0, e = 0;
        shownSpan(t, s, e);
        if (e > s) continue;
        const double x = secondsToX(s);
        if (x < kHeaderWidth - 8 || x > width() + 8) continue;
        const bool sel = m_selTriggers.contains(t.id);
        const bool hot = sel || t.id == m_flashTrigger;
        const double r = sel ? 6.0 : 5.0;
        QPolygonF d;
        d << QPointF(x, midY - r) << QPointF(x + r, midY) << QPointF(x, midY + r) << QPointF(x - r, midY);
        QColor fill = !t.enabled ? tk.ink40 : hot ? tk.accentHover : tk.accent;
        if (!t.enabled) fill.setAlpha(150);
        p.setPen(sel ? QPen(tk.ink100, 1.0) : Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(d);
        // A row of beats is a picket fence of names: skip a name that would
        // run into the next marker.
        bool room = true;
        for (const auto &o : m_triggers) {
            double os = 0, oe = 0;
            shownSpan(o, os, oe);
            if (o.id == t.id || oe > os) continue;
            const double ox = secondsToX(os);
            if (ox > x && ox - x < fm.horizontalAdvance(t.name) + r + 10) { room = false; break; }
        }
        if (!t.name.isEmpty() && room) {
            p.setPen(t.enabled ? (sel ? tk.ink100 : tk.ink60) : tk.ink40);
            p.drawText(QRectF(x + r + 4, top, 160, h - 1), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(t.name, Qt::ElideRight, 156));
        }
    }

    // A Shift/Ctrl drag selecting a stretch: a neutral dashed box.
    if (m_tdrag.mode == TriggerDrag::Span && m_tdrag.moved) {
        const double x1 = secondsToX(std::min(m_tdrag.curStart, m_tdrag.curEnd));
        const double x2 = secondsToX(std::max(m_tdrag.curStart, m_tdrag.curEnd));
        QColor fill = tk.ink100;
        fill.setAlpha(22);
        p.setPen(QPen(tk.ink60, 1.0, Qt::DashLine));
        p.setBrush(fill);
        p.drawRect(QRectF(x1 + 0.5, top + 1.5, std::max(1.0, x2 - x1 - 1.0), h - 4.0));
    }

    // A range being drawn: dashed outline until release.
    if (m_tdrag.mode == TriggerDrag::Create && m_tdrag.moved && m_tdrag.curEnd > m_tdrag.curStart) {
        const double x1 = secondsToX(m_tdrag.curStart), x2 = secondsToX(m_tdrag.curEnd);
        QColor fill = tk.accent;
        fill.setAlpha(40);
        p.setPen(QPen(tk.accent, 1.0, Qt::DashLine));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(x1 + 0.5, top + 3.5, std::max(2.0, x2 - x1 - 1.0), h - 8.0), 3, 3);
    }
    p.restore();
}

// ── Hit test ──────────────────────────────────────────────────────────────────

std::optional<TimelineCanvas::Hit> TimelineCanvas::hitTest(int x, int y) const {
    if (!m_model || x < kHeaderWidth || y < tracksTop()) return std::nullopt;
    int ti = trackAtY(y);
    if (ti < 0) return std::nullopt;
    auto *track = m_model->track(ti);
    for (int ri = int(track->regions().size()) - 1; ri >= 0; --ri) {
        const auto &r = track->regions()[ri];
        int rx1 = int(framesToX(r.timelinePosSamples));
        int rx2 = int(framesToX(r.timelineEndSamples()));
        if (x < rx1 - kEdgeTolerance || x > rx2 + kEdgeTolerance) continue;
        Hit h;
        h.trackIndex  = ti;
        h.regionIndex = ri;
        if (x <= rx1 + kEdgeTolerance) h.part = Hit::LeftEdge;
        else if (x >= rx2 - kEdgeTolerance) h.part = Hit::RightEdge;
        else h.part = Hit::Body;
        return h;
    }
    return std::nullopt;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void TimelineCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const auto &tk = Theme::tokens();

    // Background
    p.fillRect(rect(), tk.bgDeep);

    if (!m_model) return;

    // Track backgrounds
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        int y = trackY(ti);
        QRect tr(0, y, width(), m_trackHeight);
        p.fillRect(tr, (ti % 2 == 0) ? tk.bgRowAlt : tk.bgRow);
        // Track separator — recessed gap between lanes, darker than the rows.
        p.fillRect(0, y + m_trackHeight - 1, width(), 1, tk.bgDeep);
    }

    // Region waveforms
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        int y = trackY(ti);
        QRect trackRect(kHeaderWidth, y, width() - kHeaderWidth, m_trackHeight);
        for (const auto &region : m_model->track(ti)->regions())
            drawRegion(p, region, ti, trackRect);
    }

    if (m_grid.isSet()) {
        p.save();
        p.setClipRect(kHeaderWidth, tracksTop(), width() - kHeaderWidth, height() - tracksTop());
        drawBeatLines(p, tracksTop(), height() - tracksTop(), false);
        p.restore();
    }
    drawTriggerGuides(p);

    // Track headers (drawn after regions so they stay on top)
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        int y = trackY(ti);
        drawTrackHeader(p, ti, QRect(0, y, kHeaderWidth, m_trackHeight));
    }

    // Ruler and lane last-but-cursors: tracks scrolled up pass under them.
    drawRuler(p);
    drawTriggerLane(p);
    drawEditCursor(p);
    drawPlayhead(p);
}

void TimelineCanvas::drawRuler(QPainter &p) {
    const auto &tk = Theme::tokens();
    p.fillRect(0, 0, width(), kRulerHeight, tk.bgPanel);
    p.fillRect(0, kRulerHeight - 1, width(), 1, tk.divider);

    if (!m_model) return;
    int sr = m_model->sampleRate();
    double secPerPix = m_framesPerPixel / double(sr);

    // Adaptive tick spacing: choose the smallest interval that gives ≥40px between major ticks
    static const double intervals[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60, 120, 300};
    double tickInterval = 300.0;
    for (double iv : intervals) {
        if (iv / secPerPix >= 60.0) { tickInterval = iv; break; }
    }
    double subInterval = tickInterval / 5.0;

    p.setPen(tk.ink60);
    QFont f = font(); f.setPointSizeF(9.0); p.setFont(f);

    double startSec = (m_scrollX * secPerPix);
    double endSec   = startSec + width() * secPerPix;

    // Sub-ticks
    p.setPen(tk.divider);
    for (double t = std::floor(startSec / subInterval) * subInterval; t < endSec; t += subInterval) {
        int x = int(framesToX(qint64(t * sr)));
        if (x < kHeaderWidth) continue;
        p.drawLine(x, kRulerHeight - 5, x, kRulerHeight - 1);
    }

    // Major ticks + labels
    p.setPen(tk.ink60);
    for (double t = std::floor(startSec / tickInterval) * tickInterval; t < endSec; t += tickInterval) {
        int x = int(framesToX(qint64(t * sr)));
        if (x < kHeaderWidth) continue;
        p.drawLine(x, 4, x, kRulerHeight - 1);
        // Format mm:ss or ss.d
        QString label;
        int mins = int(t) / 60, secs = int(t) % 60;
        if (tickInterval < 1.0)
            label = QStringLiteral("%1.%2").arg(int(t)).arg(int(t*10) % 10);
        else if (mins > 0)
            label = QStringLiteral("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'));
        else
            label = QStringLiteral("%1s").arg(secs);
        p.drawText(x + 3, 14, label);
    }
}

void TimelineCanvas::drawTrackHeader(QPainter &p, int ti, const QRect &r) {
    auto *track = m_model->track(ti);
    const auto &tk = Theme::tokens();
    p.fillRect(r, tk.bgInteractive);
    p.fillRect(r.right(), r.top(), 1, r.height(), tk.bgDeep);

    p.setPen(tk.ink100);
    QFont f = font(); f.setPointSizeF(10.0); f.setBold(true); p.setFont(f);
    p.drawText(r.adjusted(8, 6, -4, -30), Qt::AlignLeft | Qt::AlignTop, track->name());

    // Mute / Solo indicators
    p.setFont(QFont(font().family(), 8));
    QRect muteR(r.left()+6,  r.bottom()-22, 28, 16);
    QRect soloR(r.left()+38, r.bottom()-22, 28, 16);
    p.fillRect(muteR, track->isMuted() ? tk.err : tk.bgRowHover);
    p.fillRect(soloR, track->isSoloed() ? tk.warn : tk.bgRowHover);
    p.setPen(tk.ink100);
    p.drawText(muteR, Qt::AlignCenter, QStringLiteral("M"));
    p.drawText(soloR, Qt::AlignCenter, QStringLiteral("S"));
}

void TimelineCanvas::drawRegion(QPainter &p, const audio::AudioRegion &region,
                                int /*trackIndex*/, const QRect &trackRect)
{
    int x1 = int(framesToX(region.timelinePosSamples));
    int x2 = int(framesToX(region.timelineEndSamples()));
    if (x2 < kHeaderWidth || x1 > width()) return;
    x1 = std::max(x1, kHeaderWidth);
    x2 = std::min(x2, width());

    int yTop    = trackRect.top() + 2;
    int yBottom = trackRect.bottom() - 2;
    int h       = yBottom - yTop;
    QRect rr(x1, yTop, x2 - x1, h);

    bool selected = (region.id == m_selectedRegion);
    const auto &tk = Theme::tokens();

    // Region background
    QColor bg = region.color.darker(selected ? 130 : 160);
    p.fillRect(rr, bg);
    // Border
    p.setPen(selected ? tk.warnBright : region.color.lighter(140));
    p.drawRect(rr.adjusted(0,0,-1,-1));

    // Region name
    p.setPen(tk.ink100);
    QFont f = font(); f.setPointSizeF(9.5); f.setBold(true); p.setFont(f);
    p.drawText(rr.adjusted(4, 2, -4, -h/2), Qt::AlignLeft | Qt::AlignTop,
               region.name.isEmpty() ? QStringLiteral("Region") : region.name);

    // Content — waveform peaks or whole-file spectrogram.
    if (region.sourceFile && region.sourceFile->state() == audio::AudioFile::State::Loaded) {
        bool drew = false;
        if (m_viewMode == ViewMode::Spectrogram) {
            // Spectrogram fills the body below the name strip.
            const int specTop = yTop + h / 4;
            const int specH   = yBottom - specTop - 1;
            drew = drawRegionSpectrogram(p, region, x1, x2, specTop, specH);
        }
        if (!drew) {
            const int waveTop = yTop + h / 3;
            const int waveH   = h * 2 / 3 - 2;
            drawRegionWaveform(p, region, x1, x2, waveTop, waveH);
        }
    }

    // Fade-in overlay — a darkening scrim (audio fading up from silence),
    // so it shades toward pure black, not toward a hue.
    if (region.fadeIn.durationSamples > 0) {
        int fadeW = int(double(region.fadeIn.durationSamples) / m_framesPerPixel);
        fadeW = std::min(fadeW, rr.width());
        QColor scrim = tk.bgInverse; scrim.setAlpha(180);
        QColor clear = tk.bgInverse; clear.setAlpha(0);
        QLinearGradient grad(rr.left(), 0, rr.left() + fadeW, 0);
        grad.setColorAt(0, scrim);
        grad.setColorAt(1, clear);
        p.fillRect(QRect(rr.left(), yTop, fadeW, h), grad);
    }
    // Fade-out overlay
    if (region.fadeOut.durationSamples > 0) {
        int fadeW = int(double(region.fadeOut.durationSamples) / m_framesPerPixel);
        fadeW = std::min(fadeW, rr.width());
        QColor scrim = tk.bgInverse; scrim.setAlpha(180);
        QColor clear = tk.bgInverse; clear.setAlpha(0);
        QLinearGradient grad(rr.right() - fadeW, 0, rr.right(), 0);
        grad.setColorAt(0, clear);
        grad.setColorAt(1, scrim);
        p.fillRect(QRect(rr.right() - fadeW, yTop, fadeW, h), grad);
    }
}

void TimelineCanvas::drawRegionWaveform(QPainter &p, const audio::AudioRegion &region,
                                        int x1, int x2, int waveTop, int waveH)
{
    const auto &peaks = region.sourceFile->peaks();
    const int srcCh = region.sourceFile->channelCount();
    if (peaks.empty() || srcCh == 0 || waveH <= 0) return;

    const int waveMid = waveTop + waveH / 2;
    p.setPen(region.color.lighter(180));

    const double framesPerRegionPx = m_framesPerPixel;
    const int numRegionPx = x2 - x1;
    const int regionLeftPx = int(framesToX(region.timelinePosSamples));

    for (int px = 0; px < numRegionPx; ++px) {
        qint64 regionFrame = qint64((px + (x1 - regionLeftPx)) * framesPerRegionPx);
        qint64 srcFrame    = region.srcInSamples + regionFrame;
        if (srcFrame < 0) continue;

        int peakIdx = int(srcFrame / audio::AudioFile::kPeakBlock);
        int numPeakBlocks = int(peaks.size() / srcCh);
        if (peakIdx >= numPeakBlocks) break;

        float peakVal = 0.f;
        for (int ch = 0; ch < std::min(srcCh, 2); ++ch)
            peakVal = std::max(peakVal, peaks[size_t(peakIdx * srcCh + ch)]);

        int ampPx = int(peakVal * float(waveH / 2));
        p.drawLine(x1 + px, waveMid - ampPx, x1 + px, waveMid + ampPx);
    }
}

bool TimelineCanvas::drawRegionSpectrogram(QPainter &p, const audio::AudioRegion &region,
                                           int x1, int x2, int top, int h)
{
    if (h <= 0 || x2 <= x1) return false;

    ensureSpectrogram(region.sourceFile);
    auto it = m_specImages.constFind(region.sourceFile.get());
    if (it == m_specImages.constEnd() || it.value().isNull())
        return false; // not built yet — caller falls back to the waveform

    const QImage &img = it.value();
    const qint64 fileFrames = region.sourceFile->frameCount();
    if (fileFrames <= 0) return false;

    // Map the visible on-screen slice back to a source-sample span, then to
    // image columns. xToFrames/framesToX already fold in scroll + zoom.
    const qint64 tStart = xToFrames(x1);
    const qint64 tEnd   = xToFrames(x2);
    qint64 srcStart = region.srcInSamples + (tStart - region.timelinePosSamples);
    qint64 srcEnd   = region.srcInSamples + (tEnd   - region.timelinePosSamples);
    srcStart = std::clamp<qint64>(srcStart, 0, fileFrames);
    srcEnd   = std::clamp<qint64>(srcEnd,   0, fileFrames);
    if (srcEnd <= srcStart) return false;

    int col1 = int(srcStart * img.width() / fileFrames);
    int col2 = int(srcEnd   * img.width() / fileFrames);
    col1 = std::clamp(col1, 0, img.width() - 1);
    col2 = std::clamp(col2, col1 + 1, img.width());

    const QRect target(x1, top, x2 - x1, h);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(target, img, QRect(col1, 0, col2 - col1, img.height()));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    return true;
}

void TimelineCanvas::drawEditCursor(QPainter &p) {
    int x = int(framesToX(m_editCursorFrame));
    if (x < kHeaderWidth || x >= width()) return;
    // Thin vertical line + a downward handle that sits in the ruler "header"
    // so the click position is obvious there as well as in the tracks.
    // Info blue — a cool marker is meaningful here: it distinguishes the
    // parked edit cursor from the amber playhead that moves during playback.
    const QColor col = Theme::tokens().info;
    p.setPen(QPen(col, 1.0));
    p.drawLine(x, kRulerHeight, x, height());
    p.setBrush(col);
    p.setPen(Qt::NoPen);
    QPolygon tri;
    tri << QPoint(x - 5, kRulerHeight - 9)
        << QPoint(x + 5, kRulerHeight - 9)
        << QPoint(x, kRulerHeight - 1);
    p.drawPolygon(tri);
}

void TimelineCanvas::drawPlayhead(QPainter &p) {
    if (m_playheadFrame < 0) return; // hidden while stopped
    int x = int(framesToX(m_playheadFrame));
    if (x < kHeaderWidth || x >= width()) return;
    const auto &tk = Theme::tokens();
    p.setPen(QPen(tk.accent, 1.5));
    p.drawLine(x, 0, x, height());
    // Triangle handle
    p.setBrush(tk.accent);
    p.setPen(Qt::NoPen);
    QPolygon tri;
    tri << QPoint(x-5, 0) << QPoint(x+5, 0) << QPoint(x, 10);
    p.drawPolygon(tri);
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void TimelineCanvas::mousePressEvent(QMouseEvent *e) {
    if (!m_model) return;
    int x = e->pos().x(), y = e->pos().y();

    // The lighting lane's header cell is just a label.
    if (x < kHeaderWidth && inTriggerLane(y)) return;

    // Click on track header mute/solo buttons
    if (x < kHeaderWidth && y >= tracksTop()) {
        int ti = trackAtY(y);
        if (ti >= 0) {
            auto *track = m_model->track(ti);
            int localY = y - trackY(ti);
            int localX = x;
            QRect muteR(6,  m_trackHeight-22, 28, 16);
            QRect soloR(38, m_trackHeight-22, 28, 16);
            if (muteR.contains(localX, localY)) { track->setMuted(!track->isMuted()); update(); return; }
            if (soloR.contains(localX, localY)) { track->setSoloed(!track->isSoloed()); update(); return; }
            emit trackSelected(ti);
            return;
        }
    }

    // Any click in the time area (ruler or tracks) repositions the edit
    // cursor — this is what makes the ruler "header" marker jump to the
    // click, and where preview playback then starts from (Audacity-style).
    if (x >= kHeaderWidth) {
        qint64 f = std::max<qint64>(0, xToFrames(x));
        if (f != m_editCursorFrame) { m_editCursorFrame = f; emit editCursorMoved(f); }
    }

    // Clicks in the ruler only move the cursor — no region interaction.
    if (y < kRulerHeight) { update(); return; }

    // Lighting lane: select / start moving or resizing a trigger, or start
    // making a new one (a click makes a point, a drag a range — decided on
    // release). Left button only: a right press is the context menu's.
    if (inTriggerLane(y)) {
        if (e->button() == Qt::LeftButton) {
            m_tdrag = TriggerDrag{};
            m_tdrag.pressPos = e->pos();
            m_tdrag.pressSec = xToSeconds(x);
            m_tdrag.noSnap = e->modifiers() & Qt::AltModifier;
            m_tdrag.pressSnapped = snapSeconds(m_tdrag.pressSec, m_tdrag.noSnap);
            TriggerPart part = TriggerPart::Body;
            const int ti = triggerAt(x, &part);
            const auto mods = e->modifiers();
            const bool pick = mods & (Qt::ShiftModifier | Qt::ControlModifier);
            if (ti >= 0 && pick) {
                // Shift / Ctrl: change the selection only, no drag.
                const QUuid id = m_triggers[size_t(ti)].id;
                if (mods & Qt::ControlModifier) {
                    if (!m_selTriggers.remove(id)) m_selTriggers.insert(id);
                } else {
                    m_selTriggers.insert(id);
                }
                m_tdrag = TriggerDrag{};
                emit triggerClicked(id, mods);
                update();
                return;
            }
            if (ti >= 0) {
                const auto t = m_triggers[size_t(ti)];
                const bool edge = part != TriggerPart::Body;
                if (m_selTriggers.contains(t.id) && m_selTriggers.size() > 1 && !edge
                    && !(mods & Qt::AltModifier)) {
                    // Grabbing part of a selection: drag them all; a click
                    // without a drag reselects on release.
                    m_tdrag.clickOnRelease = true;
                } else {
                    m_selTriggers = {t.id};
                    emit triggerClicked(t.id, mods);   // may come back wider (its group)
                }
                m_tdrag.id = t.id;
                m_tdrag.origStart = m_tdrag.curStart = t.start;
                m_tdrag.origEnd   = m_tdrag.curEnd   = t.isRange() ? t.end : -1.0;
                if (edge) {
                    m_tdrag.mode = part == TriggerPart::StartEdge ? TriggerDrag::ResizeStart
                                                                  : TriggerDrag::ResizeEnd;
                } else if (m_selTriggers.size() > 1 && m_selTriggers.contains(t.id)) {
                    m_tdrag.mode = TriggerDrag::MoveMany;
                    m_tdrag.minStart = t.start;
                    for (const auto &o : m_triggers)
                        if (m_selTriggers.contains(o.id))
                            m_tdrag.minStart = std::min(m_tdrag.minStart, o.start);
                } else {
                    m_tdrag.mode = TriggerDrag::Move;
                }
            } else if (pick) {
                m_tdrag.mode = TriggerDrag::Span;
                m_tdrag.curStart = m_tdrag.curEnd = m_tdrag.pressSec;
            } else {
                m_tdrag.mode = TriggerDrag::Create;
                m_tdrag.curStart = m_tdrag.pressSnapped;
            }
        }
        update();
        return;
    }

    auto hit = hitTest(x, y);

    if (m_tool == Tool::Razor) {
        if (hit) {
            qint64 splitAt = xToFrames(x);
            auto &region = m_model->track(hit->trackIndex)->regions()[hit->regionIndex];
            m_model->splitRegion(region.id, splitAt);
        }
        return;
    }

    // Select tool
    if (hit) {
        m_selectedRegion = m_model->track(hit->trackIndex)->regions()[hit->regionIndex].id;
        emit regionSelected(m_selectedRegion);

        auto &region = m_model->track(hit->trackIndex)->regions()[hit->regionIndex];
        m_drag.active        = true;
        m_drag.moved         = false;
        m_drag.regionId      = region.id;
        m_drag.trackIndex    = hit->trackIndex;
        m_drag.isTrim        = (hit->part != Hit::Body);
        m_drag.trimLeft      = (hit->part == Hit::LeftEdge);
        m_drag.dragStartFrame  = xToFrames(x);
        m_drag.regionStartPos  = region.timelinePosSamples;
        m_drag.regionSrcIn     = region.srcInSamples;
        m_drag.regionSrcOut    = (region.srcOutSamples < 0 && region.sourceFile)
                                  ? region.sourceFile->frameCount() : region.srcOutSamples;
        m_drag.mouseStart    = e->pos();
    } else {
        m_selectedRegion = QUuid();
        emit regionSelected(QUuid());
    }
    update();
}

void TimelineCanvas::mouseMoveEvent(QMouseEvent *e) {
    int x = e->pos().x(), y = e->pos().y();

    // ── Lighting lane drag ─────────────────────────────────────────────
    if (m_tdrag.mode != TriggerDrag::None) {
        if (!m_tdrag.moved) {
            if (std::abs(x - m_tdrag.pressPos.x()) < kDragThreshold) return;
            m_tdrag.moved = true;
        }
        const double sec   = xToSeconds(x);
        const double delta = sec - m_tdrag.pressSec;
        // Alt at the press or now: this gesture goes where the mouse is.
        const bool noSnap = m_tdrag.noSnap || (e->modifiers() & Qt::AltModifier);
        auto snap = [this, noSnap](double s) { return snapSeconds(s, noSnap); };
        // Shortest range a drag can leave: a few pixels, so it stays grabbable.
        const double minLen = std::max(0.001, 4.0 * m_framesPerPixel / m_triggerRate);
        switch (m_tdrag.mode) {
        case TriggerDrag::Create: {
            const double a = snap(m_tdrag.pressSec), b = snap(sec);
            m_tdrag.curStart = std::min(a, b);
            m_tdrag.curEnd   = std::max(a, b);
            break;
        }
        case TriggerDrag::Span:
            m_tdrag.curStart = m_tdrag.pressSec;
            m_tdrag.curEnd   = sec;
            break;
        case TriggerDrag::MoveMany: {
            // The grabbed one snaps; the rest keep their spacing. Nothing
            // goes before the start of the song.
            double s = snap(m_tdrag.origStart + delta);
            s = std::max(s, m_tdrag.origStart - m_tdrag.minStart);
            m_tdrag.curStart = s;
            setCursor(Qt::ClosedHandCursor);
            break;
        }
        case TriggerDrag::Move: {
            const double len = m_tdrag.origEnd > m_tdrag.origStart
                                   ? m_tdrag.origEnd - m_tdrag.origStart : 0.0;
            m_tdrag.curStart = std::max(0.0, snap(m_tdrag.origStart + delta));
            m_tdrag.curEnd   = len > 0.0 ? m_tdrag.curStart + len : -1.0;
            setCursor(Qt::ClosedHandCursor);
            break;
        }
        case TriggerDrag::ResizeStart:
            m_tdrag.curStart = std::clamp(snap(sec), 0.0, m_tdrag.origEnd - minLen);
            break;
        case TriggerDrag::ResizeEnd:
            m_tdrag.curEnd = std::max(snap(sec), m_tdrag.origStart + minLen);
            break;
        case TriggerDrag::None:
            break;
        }
        update();
        return;
    }
    if (inTriggerLane(y) && x >= kHeaderWidth && !m_drag.active) {
        TriggerPart part = TriggerPart::Body;
        const int ti = triggerAt(x, &part);
        setCursor(ti >= 0 && part != TriggerPart::Body ? Qt::SizeHorCursor
                  : ti >= 0                            ? Qt::OpenHandCursor
                                                       : Qt::ArrowCursor);
        return;
    }

    // Hover cursor (only while not mid-drag, so an active trim/move keeps
    // its own cursor). A region body shows the normal arrow — only the
    // trim edges get a resize cursor. The old code put a 4-way "move"
    // cursor over the whole body, which read as if everything would shift.
    if (!m_drag.active) {
        if (m_tool == Tool::Razor) {
            setCursor(Qt::CrossCursor);
        } else {
            auto hit = hitTest(x, y);
            if (hit && hit->part != Hit::Body)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::ArrowCursor);
        }
    }

    if (!m_drag.active || !m_model) return;

    // A body move only begins once the pointer has travelled past the
    // threshold; until then the press is treated as a plain click (which
    // already set the edit cursor and selected the region).
    if (!m_drag.isTrim && !m_drag.moved) {
        if (std::abs(e->pos().x() - m_drag.mouseStart.x()) < kDragThreshold &&
            std::abs(e->pos().y() - m_drag.mouseStart.y()) < kDragThreshold)
            return;
        m_drag.moved = true;
        setCursor(Qt::ClosedHandCursor); // feedback only while actually moving
    }

    qint64 currentFrame = xToFrames(x);
    qint64 delta = currentFrame - m_drag.dragStartFrame;

    if (m_drag.isTrim) {
        if (m_drag.trimLeft) {
            qint64 newSrcIn = std::max(qint64(0), m_drag.regionSrcIn + delta);
            m_model->trimRegion(m_drag.regionId, true, newSrcIn);
        } else {
            qint64 newSrcOut = m_drag.regionSrcOut + delta;
            if (m_drag.regionId == m_drag.regionId) {
                auto [ti, ri] = m_model->findRegion(m_drag.regionId);
                if (ti >= 0 && m_model->track(ti)->regions()[ri].sourceFile) {
                    qint64 maxOut = m_model->track(ti)->regions()[ri].sourceFile->frameCount();
                    newSrcOut = std::clamp(newSrcOut, m_drag.regionSrcIn + 1, maxOut);
                }
            }
            m_model->trimRegion(m_drag.regionId, false, newSrcOut);
        }
    } else {
        qint64 newPos = std::max(qint64(0), m_drag.regionStartPos + delta);
        m_model->moveRegion(m_drag.regionId, newPos);
    }
}

void TimelineCanvas::mouseReleaseEvent(QMouseEvent *) {
    m_drag.active = false;
    m_drag.moved  = false;
    setCursor(Qt::ArrowCursor);

    if (m_tdrag.mode == TriggerDrag::None) return;
    // Reset first: the signals below come straight back as setTriggers().
    const TriggerDrag d = m_tdrag;
    m_tdrag = TriggerDrag{};
    if (d.mode == TriggerDrag::Create) {
        if (d.moved && d.curEnd > d.curStart) emit triggerAdded(d.curStart, d.curEnd);
        else                                  emit triggerAdded(d.pressSnapped, -1.0);
    } else if (d.mode == TriggerDrag::Span) {
        if (d.moved) {
            const double a = std::min(d.curStart, d.curEnd), b = std::max(d.curStart, d.curEnd);
            for (const auto &t : m_triggers)
                if (t.start >= a && t.start <= b) m_selTriggers.insert(t.id);
            emit triggersSpanSelected(a, b);
        }
    } else if (d.mode == TriggerDrag::MoveMany) {
        const double delta = d.curStart - d.origStart;
        if (d.moved && delta != 0.0) {
            QList<QUuid> ids;
            for (const auto &t : m_triggers)
                if (m_selTriggers.contains(t.id)) ids << t.id;
            emit triggersMovedBy(ids, delta);
        } else if (!d.moved && d.clickOnRelease) {
            m_selTriggers = {d.id};
            emit triggerClicked(d.id, Qt::NoModifier);
        }
    } else if (d.moved) {
        emit triggerMoved(d.id, d.curStart, d.curEnd);
    }
    update();
}

void TimelineCanvas::mouseDoubleClickEvent(QMouseEvent *e) {
    // Double-click a trigger in the lighting lane: select + rename. (Track
    // header rename is future work.)
    const int x = e->pos().x();
    if (x < kHeaderWidth || !inTriggerLane(e->pos().y())) return;
    const int ti = triggerAt(x);
    if (ti < 0) return;
    const QUuid id = m_triggers[size_t(ti)].id;
    m_tdrag = TriggerDrag{};
    emit triggerContextAction(id, QStringLiteral("rename"));
}

void TimelineCanvas::wheelEvent(QWheelEvent *e) {
    // Ctrl+wheel = zoom (snaps for predictable feel).
    if (e->modifiers() & Qt::ControlModifier) {
        double factor = (e->angleDelta().y() > 0) ? 0.8 : 1.25;
        setFramesPerPixel(m_framesPerPixel * factor);
        return;
    }

    // Plain / Shift wheel = smooth pan. Re-target the running animation
    // each tick so rapid rolls stack into a single longer glide.
    auto smoothTo = [this](QScrollBar *bar, int target) {
        if (!bar) return;
        target = qBound(bar->minimum(), target, bar->maximum());
        if (!m_scrollAnim) {
            m_scrollAnim = new QPropertyAnimation(bar, "value", this);
            m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_scrollAnim->setDuration(220);
        } else if (m_scrollAnim->targetObject() != bar) {
            m_scrollAnim->setTargetObject(bar);
        }
        m_scrollAnim->stop();
        m_scrollAnim->setStartValue(bar->value());
        m_scrollAnim->setEndValue(target);
        m_scrollAnim->start();
    };

    const int dy = e->angleDelta().y();
    if (e->modifiers() & Qt::ShiftModifier) {
        if (m_hbar) smoothTo(m_hbar, m_hbar->value() - dy);
    } else {
        if (m_vbar) smoothTo(m_vbar, m_vbar->value() - dy / 2);
    }
}

void TimelineCanvas::resizeEvent(QResizeEvent *) {
    updateScrollBars();
}

void TimelineCanvas::contextMenuEvent(QContextMenuEvent *e) {
    if (!m_model) return;

    QMenu menu(this);
    const int x = e->pos().x();
    const int y = e->pos().y();

    // Right-click in the lighting lane: trigger-scoped actions.
    if (inTriggerLane(y)) {
        if (x < kHeaderWidth) return;
        const int ti = triggerAt(x);
        if (ti < 0) {
            auto *addAct = menu.addAction(tr("Add Trigger Here"));
            const double sec = snapSeconds(xToSeconds(x), e->modifiers() & Qt::AltModifier);
            if (menu.exec(e->globalPos()) == addAct) emit triggerAdded(sec, -1.0);
            return;
        }
        const auto t = m_triggers[size_t(ti)];
        if (!m_selTriggers.contains(t.id)) {
            m_selTriggers = {t.id};
            emit triggerClicked(t.id, Qt::NoModifier);   // may widen to its group
        }
        update();
        const int n = int(m_selTriggers.size());
        QAction *renameAct = nullptr, *rangeAct = nullptr;
        if (n == 1) {
            renameAct = menu.addAction(tr("Rename…"));
            rangeAct  = menu.addAction(t.isRange() ? tr("Make a Point") : tr("Make a Range"));
        }
        auto *enableAct = menu.addAction(n > 1 ? (t.enabled ? tr("Disable %1 Triggers").arg(n)
                                                            : tr("Enable %1 Triggers").arg(n))
                                               : (t.enabled ? tr("Disable") : tr("Enable")));
        menu.addSeparator();
        auto *groupAct = menu.addAction(n > 1 ? tr("Group %1 Triggers…").arg(n) : tr("Group…"));
        QAction *ungroupAct = nullptr, *selectGroupAct = nullptr;
        if (!t.group.isEmpty()) {
            selectGroupAct = menu.addAction(tr("Select Group \"%1\"").arg(t.group));
            ungroupAct = menu.addAction(tr("Ungroup"));
        }
        menu.addSeparator();
        auto *deleteAct = menu.addAction(n > 1 ? tr("Delete %1 Triggers").arg(n) : tr("Delete"));
        QAction *chosen = menu.exec(e->globalPos());
        QString action;
        if      (chosen && chosen == renameAct)      action = QStringLiteral("rename");
        else if (chosen && chosen == rangeAct)       action = QStringLiteral("toggleRange");
        else if (chosen == enableAct)                action = QStringLiteral("toggleEnabled");
        else if (chosen == groupAct)                 action = QStringLiteral("group");
        else if (chosen && chosen == ungroupAct)     action = QStringLiteral("ungroup");
        else if (chosen && chosen == selectGroupAct) action = QStringLiteral("selectGroup");
        else if (chosen == deleteAct)                action = QStringLiteral("delete");
        if (!action.isEmpty()) emit triggerContextAction(t.id, action);
        return;
    }

    // Right-click on the track-header strip (left side): track-scoped actions.
    if (x < kHeaderWidth && y >= tracksTop()) {
        const int ti = trackAtY(y);
        if (ti < 0) {
            // Empty header area below the last track — just offer Add Track.
            auto *addAct = menu.addAction(tr("Add Track"));
            if (menu.exec(e->globalPos()) == addAct) emit requestAddTrack();
            return;
        }
        auto *track = m_model->track(ti);
        const QString tname = track ? track->name() : tr("Track %1").arg(ti + 1);

        auto *addTrackAct = menu.addAction(tr("Add Track"));
        menu.addSeparator();
        auto *renameAct = menu.addAction(tr("Rename \"%1\"…").arg(tname));
        auto *muteAct   = menu.addAction(track && track->isMuted()  ? tr("Unmute") : tr("Mute"));
        auto *soloAct   = menu.addAction(track && track->isSoloed() ? tr("Unsolo") : tr("Solo"));
        menu.addSeparator();
        auto *removeAct = menu.addAction(tr("Remove Track"));
        removeAct->setShortcut(QKeySequence::Delete);

        QAction *chosen = menu.exec(e->globalPos());
        if (!chosen) return;
        if (chosen == addTrackAct) {
            emit requestAddTrack();
        } else if (chosen == removeAct) {
            // Refuse to remove the last track — the editor needs at least
            // one to draw against. The button could be disabled instead,
            // but a status hint feels less surprising.
            if (m_model->trackCount() <= 1) return;
            const auto resp = QMessageBox::question(this,
                tr("Remove Track"),
                tr("Remove track \"%1\" and all its regions? This cannot be undone.").arg(tname),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (resp == QMessageBox::Yes) m_model->removeTrack(ti);
        } else if (chosen == muteAct && track) {
            track->setMuted(!track->isMuted());
        } else if (chosen == soloAct && track) {
            track->setSoloed(!track->isSoloed());
        } else if (chosen == renameAct && track) {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("Rename Track"),
                tr("Track name:"), QLineEdit::Normal, tname, &ok);
            if (ok && !n.trimmed().isEmpty()) track->setName(n.trimmed());
        }
        update();
        return;
    }

    // Right-click on a region: region-scoped actions.
    if (auto hit = hitTest(x, y)) {
        const auto &region = m_model->track(hit->trackIndex)->regions()[hit->regionIndex];
        const QUuid rid = region.id;
        auto *splitAct  = menu.addAction(tr("Split at Cursor"));
        auto *removeAct = menu.addAction(tr("Remove Region"));
        removeAct->setShortcut(QKeySequence::Delete);

        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == splitAct) {
            m_model->splitRegion(rid, xToFrames(x));
        } else if (chosen == removeAct) {
            m_model->removeRegion(rid);
        }
    }
}

} // namespace quewi::ui
