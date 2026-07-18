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
    return kRulerHeight + idx * m_trackHeight - m_scrollY;
}

int TimelineCanvas::contentHeight() const {
    if (!m_model) return kRulerHeight;
    return kRulerHeight + m_model->trackCount() * m_trackHeight;
}

double TimelineCanvas::framesToX(qint64 frames) const {
    return kHeaderWidth + double(frames) / m_framesPerPixel - m_scrollX;
}

qint64 TimelineCanvas::xToFrames(int x) const {
    return qint64((double(x - kHeaderWidth) + m_scrollX) * m_framesPerPixel);
}

int TimelineCanvas::trackAtY(int y) const {
    int idx = (y - kRulerHeight + m_scrollY) / m_trackHeight;
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
        m_vbar->setPageStep(height() - kRulerHeight);
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

// ── Hit test ──────────────────────────────────────────────────────────────────

std::optional<TimelineCanvas::Hit> TimelineCanvas::hitTest(int x, int y) const {
    if (!m_model || x < kHeaderWidth || y < kRulerHeight) return std::nullopt;
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

    // Track headers (drawn after regions so they stay on top)
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        int y = trackY(ti);
        drawTrackHeader(p, ti, QRect(0, y, kHeaderWidth, m_trackHeight));
    }

    drawRuler(p);
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

    // Click on track header mute/solo buttons
    if (x < kHeaderWidth && y >= kRulerHeight) {
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
}

void TimelineCanvas::mouseDoubleClickEvent(QMouseEvent *e) {
    // Double-click on track header: rename (future work)
    Q_UNUSED(e)
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

    // Right-click on the track-header strip (left side): track-scoped actions.
    if (x < kHeaderWidth && y >= kRulerHeight) {
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
