#include "ui/TimelineCanvas.h"
#include "audio/AudioFile.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <cmath>
#include <algorithm>

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

    // Background
    p.fillRect(rect(), QColor(28, 30, 38));

    if (!m_model) return;

    // Track backgrounds
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        int y = trackY(ti);
        QRect tr(0, y, width(), m_trackHeight);
        p.fillRect(tr, (ti % 2 == 0) ? QColor(32, 35, 44) : QColor(36, 39, 50));
        // Track separator
        p.fillRect(0, y + m_trackHeight - 1, width(), 1, QColor(20, 22, 28));
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
    drawPlayhead(p);
}

void TimelineCanvas::drawRuler(QPainter &p) {
    p.fillRect(0, 0, width(), kRulerHeight, QColor(24, 26, 34));
    p.fillRect(0, kRulerHeight - 1, width(), 1, QColor(50, 55, 70));

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

    p.setPen(QColor(130, 140, 160));
    QFont f = font(); f.setPointSizeF(9.0); p.setFont(f);

    double startSec = (m_scrollX * secPerPix);
    double endSec   = startSec + width() * secPerPix;

    // Sub-ticks
    p.setPen(QColor(55, 62, 80));
    for (double t = std::floor(startSec / subInterval) * subInterval; t < endSec; t += subInterval) {
        int x = int(framesToX(qint64(t * sr)));
        if (x < kHeaderWidth) continue;
        p.drawLine(x, kRulerHeight - 5, x, kRulerHeight - 1);
    }

    // Major ticks + labels
    p.setPen(QColor(140, 150, 170));
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
    p.fillRect(r, QColor(40, 44, 56));
    p.fillRect(r.right(), r.top(), 1, r.height(), QColor(20, 22, 28));

    p.setPen(QColor(200, 205, 215));
    QFont f = font(); f.setPointSizeF(10.0); f.setBold(true); p.setFont(f);
    p.drawText(r.adjusted(8, 6, -4, -30), Qt::AlignLeft | Qt::AlignTop, track->name());

    // Mute / Solo indicators
    p.setFont(QFont(font().family(), 8));
    QRect muteR(r.left()+6,  r.bottom()-22, 28, 16);
    QRect soloR(r.left()+38, r.bottom()-22, 28, 16);
    p.fillRect(muteR, track->isMuted() ? QColor(220,80,80) : QColor(55,60,75));
    p.fillRect(soloR, track->isSoloed() ? QColor(220,180,30) : QColor(55,60,75));
    p.setPen(Qt::white);
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

    // Region background
    QColor bg = region.color.darker(selected ? 130 : 160);
    p.fillRect(rr, bg);
    // Border
    p.setPen(selected ? QColor(255,220,60) : region.color.lighter(140));
    p.drawRect(rr.adjusted(0,0,-1,-1));

    // Region name
    p.setPen(QColor(230, 235, 245));
    QFont f = font(); f.setPointSizeF(9.5); f.setBold(true); p.setFont(f);
    p.drawText(rr.adjusted(4, 2, -4, -h/2), Qt::AlignLeft | Qt::AlignTop,
               region.name.isEmpty() ? QStringLiteral("Region") : region.name);

    // Waveform
    if (!region.sourceFile || region.sourceFile->state() != audio::AudioFile::State::Loaded) return;
    const auto &peaks = region.sourceFile->peaks();
    int srcCh = region.sourceFile->channelCount();
    int sr    = region.sourceFile->sampleRate();
    if (peaks.empty() || srcCh == 0) return;

    int waveTop = yTop + h / 3;
    int waveH   = h * 2 / 3 - 2;
    int waveMid = waveTop + waveH / 2;

    p.setPen(region.color.lighter(180));

    double framesPerRegionPx = m_framesPerPixel;
    int numRegionPx = x2 - x1;

    for (int px = 0; px < numRegionPx; ++px) {
        qint64 regionFrame = qint64((px + (x1 - int(framesToX(region.timelinePosSamples)))) * framesPerRegionPx);
        qint64 srcFrame    = region.srcInSamples + regionFrame;
        if (srcFrame < 0) continue;

        // Map srcFrame to peak block
        int peakIdx = int(srcFrame / audio::AudioFile::kPeakBlock);
        int numPeakBlocks = int(peaks.size() / srcCh);
        if (peakIdx >= numPeakBlocks) break;

        float peakVal = 0.f;
        for (int ch = 0; ch < std::min(srcCh, 2); ++ch)
            peakVal = std::max(peakVal, peaks[size_t(peakIdx * srcCh + ch)]);

        int ampPx = int(peakVal * float(waveH / 2));
        p.drawLine(x1 + px, waveMid - ampPx, x1 + px, waveMid + ampPx);
    }

    // Fade-in overlay
    if (region.fadeIn.durationSamples > 0) {
        int fadeW = int(double(region.fadeIn.durationSamples) / m_framesPerPixel);
        fadeW = std::min(fadeW, rr.width());
        QLinearGradient grad(rr.left(), 0, rr.left() + fadeW, 0);
        grad.setColorAt(0, QColor(0,0,0,180));
        grad.setColorAt(1, QColor(0,0,0,0));
        p.fillRect(QRect(rr.left(), yTop, fadeW, h), grad);
    }
    // Fade-out overlay
    if (region.fadeOut.durationSamples > 0) {
        int fadeW = int(double(region.fadeOut.durationSamples) / m_framesPerPixel);
        fadeW = std::min(fadeW, rr.width());
        QLinearGradient grad(rr.right() - fadeW, 0, rr.right(), 0);
        grad.setColorAt(0, QColor(0,0,0,0));
        grad.setColorAt(1, QColor(0,0,0,180));
        p.fillRect(QRect(rr.right() - fadeW, yTop, fadeW, h), grad);
    }
}

void TimelineCanvas::drawPlayhead(QPainter &p) {
    int x = int(framesToX(m_playheadFrame));
    if (x < kHeaderWidth || x >= width()) return;
    p.setPen(QPen(QColor(255, 60, 60), 1.5));
    p.drawLine(x, 0, x, height());
    // Triangle handle
    p.setBrush(QColor(255, 60, 60));
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

    // Update cursor based on hover
    if (m_tool == Tool::Razor) {
        setCursor(Qt::CrossCursor);
    } else {
        auto hit = hitTest(x, y);
        if (hit && hit->part != Hit::Body)
            setCursor(Qt::SizeHorCursor);
        else if (hit)
            setCursor(Qt::SizeAllCursor);
        else
            setCursor(Qt::ArrowCursor);
    }

    if (!m_drag.active || !m_model) return;

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
}

void TimelineCanvas::mouseDoubleClickEvent(QMouseEvent *e) {
    // Double-click on track header: rename (future work)
    Q_UNUSED(e)
}

void TimelineCanvas::wheelEvent(QWheelEvent *e) {
    // Ctrl+wheel = zoom; plain wheel = scroll
    if (e->modifiers() & Qt::ControlModifier) {
        double factor = (e->angleDelta().y() > 0) ? 0.8 : 1.25;
        setFramesPerPixel(m_framesPerPixel * factor);
    } else if (e->modifiers() & Qt::ShiftModifier) {
        if (m_hbar) m_hbar->setValue(m_hbar->value() - e->angleDelta().y());
    } else {
        if (m_vbar) m_vbar->setValue(m_vbar->value() - e->angleDelta().y() / 2);
    }
}

void TimelineCanvas::resizeEvent(QResizeEvent *) {
    updateScrollBars();
}

} // namespace quewi::ui
