#include "ui/WaveformWidget.h"

#include "audio/AudioFile.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace quewi::ui {

namespace {
constexpr int kHitZonePx = 8;
constexpr int kHandleWidth = 3;

const QColor kBg(0x16, 0x18, 0x1D);
const QColor kMid(0x33, 0x37, 0x3F);
const QColor kWave(0x62, 0xB4, 0xFF);
const QColor kWaveDim(0x33, 0x66, 0x99);
const QColor kTrimBar(0xF2, 0xC9, 0x4C);
const QColor kFadeFill(0xA8, 0x8B, 0xFF, 96);
const QColor kFadeEdge(0xA8, 0x8B, 0xFF);
const QColor kTextCol(0xA8, 0xAE, 0xBA);
} // namespace

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(96);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
}

WaveformWidget::~WaveformWidget() = default;

void WaveformWidget::setAudioFile(std::shared_ptr<audio::AudioFile> file)
{
    if (m_file) disconnect(m_file.get(), nullptr, this, nullptr);
    m_file = std::move(file);
    if (m_file) {
        connect(m_file.get(), &audio::AudioFile::stateChanged,
                this, [this]{ update(); });
    }
    update();
}

void WaveformWidget::setEditMode(EditMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    setCursor(mode == EditMode::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
}

void WaveformWidget::setMarkers(double trimInSec, double trimOutSec,
                                double fadeInSec, double fadeOutSec)
{
    m_trimIn  = std::max(0.0, trimInSec);
    m_trimOut = std::max(0.0, trimOutSec);
    m_fadeIn  = std::max(0.0, fadeInSec);
    m_fadeOut = std::max(0.0, fadeOutSec);
    update();
}

double WaveformWidget::effectiveDuration() const
{
    if (!m_file || m_file->state() != audio::AudioFile::State::Loaded) return 0.0;
    return m_file->durationSeconds();
}

double WaveformWidget::viewSpan() const
{
    if (m_viewEnd > m_viewStart) return m_viewEnd - m_viewStart;
    return effectiveDuration();
}

void WaveformWidget::clampView()
{
    const double dur = effectiveDuration();
    if (dur <= 0.0) { m_viewStart = 0.0; m_viewEnd = 0.0; return; }
    if (m_viewEnd <= m_viewStart) { m_viewStart = 0.0; m_viewEnd = 0.0; return; }
    // Don't allow a window narrower than ~5 ms — keeps the renderer
    // sensible and prevents the user from zooming so far in that one
    // pixel covers a sub-sample.
    const double minSpan = 0.005;
    double span = std::max(minSpan, m_viewEnd - m_viewStart);
    span = std::min(span, dur);
    if (m_viewStart < 0.0)          m_viewStart = 0.0;
    if (m_viewStart + span > dur)   m_viewStart = dur - span;
    if (m_viewStart < 0.0)          m_viewStart = 0.0;
    m_viewEnd = m_viewStart + span;
    if (m_viewEnd >= dur && m_viewStart <= 0.0) {
        // Fully zoomed out — collapse to "no-zoom" sentinel.
        m_viewStart = 0.0;
        m_viewEnd   = 0.0;
    }
}

int WaveformWidget::secondsToPixel(double sec) const
{
    const double span = viewSpan();
    if (span <= 0.0 || width() <= 0) return 0;
    return static_cast<int>(((sec - m_viewStart) / span) * width());
}

double WaveformWidget::pixelToSeconds(int px) const
{
    const double span = viewSpan();
    if (span <= 0.0 || width() <= 0) return m_viewStart;
    const double t = m_viewStart + (static_cast<double>(px) / width()) * span;
    return std::clamp(t, 0.0, effectiveDuration());
}

double WaveformWidget::snapToZeroCrossing(double seconds) const
{
    if (!m_file || m_file->state() != audio::AudioFile::State::Loaded) return seconds;
    const auto &samples = m_file->samples();
    const int chans = m_file->channelCount();
    const int sr    = m_file->sampleRate();
    const qint64 totalFrames = m_file->frameCount();
    if (chans <= 0 || sr <= 0 || totalFrames <= 1) return seconds;

    const qint64 target = static_cast<qint64>(seconds * sr);
    const qint64 win    = static_cast<qint64>(0.05 * sr);  // ±50 ms
    const qint64 lo     = std::max<qint64>(0, target - win);
    const qint64 hi     = std::min<qint64>(totalFrames - 1, target + win);
    if (lo >= hi) return seconds;

    qint64 best = -1;
    qint64 bestDist = std::numeric_limits<qint64>::max();
    // Look at channel 0 only — for stereo content the L channel is
    // representative enough and avoids surprising behaviour where
    // a "zero" exists on R but not L.
    for (qint64 f = lo; f < hi; ++f) {
        const float a = samples[static_cast<size_t>(f)     * chans];
        const float b = samples[static_cast<size_t>(f + 1) * chans];
        const bool crossed = (a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f);
        if (!crossed) continue;
        const qint64 d = std::llabs(f - target);
        if (d < bestDist) { bestDist = d; best = f; }
    }
    if (best < 0) return seconds;
    return static_cast<double>(best) / sr;
}

WaveformWidget::Handle WaveformWidget::hitTest(int x) const
{
    if (m_mode == EditMode::None) return Handle::None;
    const double dur = effectiveDuration();
    if (dur <= 0.0) return Handle::None;

    if (m_mode == EditMode::Trim) {
        const int xIn  = secondsToPixel(m_trimIn);
        const int xOut = secondsToPixel(m_trimOut > 0 ? m_trimOut : dur);
        if (std::abs(x - xIn)  <= kHitZonePx) return Handle::TrimIn;
        if (std::abs(x - xOut) <= kHitZonePx) return Handle::TrimOut;
    } else if (m_mode == EditMode::Fade) {
        const int xFadeIn  = secondsToPixel(m_fadeIn);
        const int xFadeOut = secondsToPixel(dur - m_fadeOut);
        if (std::abs(x - xFadeIn)  <= kHitZonePx) return Handle::FadeIn;
        if (std::abs(x - xFadeOut) <= kHitZonePx) return Handle::FadeOut;
    }
    return Handle::None;
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);

    if (!m_file || m_file->state() == audio::AudioFile::State::Empty) {
        p.setPen(kTextCol);
        p.drawText(rect(), Qt::AlignCenter, tr("No file loaded"));
        return;
    }
    if (m_file->state() == audio::AudioFile::State::Loading) {
        p.setPen(kTextCol);
        p.drawText(rect(), Qt::AlignCenter, tr("Loading…"));
        return;
    }
    if (m_file->state() == audio::AudioFile::State::Failed) {
        p.setPen(QColor(0xFF, 0x5A, 0x5A));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Failed: %1").arg(m_file->errorString()));
        return;
    }

    const auto &peaks = m_file->peaks();
    const int chans = m_file->channelCount();
    if (peaks.empty() || chans <= 0) return;

    const int totalRows = static_cast<int>(peaks.size() / chans);
    const int w = width();
    const int h = height();
    const double dur = effectiveDuration();

    p.setPen(kMid);
    p.drawLine(0, h / 2, w, h / 2);

    // Compute trim window in pixels (defaults to full width).
    const int trimXIn  = secondsToPixel(m_trimIn);
    const int trimXOut = secondsToPixel(m_trimOut > 0 ? m_trimOut : dur);

    // Map x → peak row through the visible window. When zoomed in,
    // each pixel covers a small slice of the file so the row range is
    // narrow; we still pick the max peak in the slice for an accurate
    // envelope.
    const double viewS = m_viewStart;
    const double viewE = (m_viewEnd > m_viewStart) ? m_viewEnd : dur;
    const double rowsPerSec = dur > 0.0 ? totalRows / dur : 0.0;

    for (int x = 0; x < w; ++x) {
        const double t0 = viewS + (static_cast<double>(x)     / w) * (viewE - viewS);
        const double t1 = viewS + (static_cast<double>(x + 1) / w) * (viewE - viewS);
        const int rowStart = std::clamp(int(t0 * rowsPerSec), 0, totalRows);
        const int rowEnd   = std::clamp(int(t1 * rowsPerSec), rowStart, totalRows);
        float peak = 0.0f;
        for (int r = rowStart; r <= rowEnd && r < totalRows; ++r) {
            for (int c = 0; c < chans; ++c) {
                const float v = peaks[static_cast<size_t>(r) * static_cast<size_t>(chans)
                                       + static_cast<size_t>(c)];
                if (v > peak) peak = v;
            }
        }
        const int half = static_cast<int>(peak * (h / 2 - 2));
        const bool insideTrim = (x >= trimXIn && x <= trimXOut);
        p.setPen(insideTrim ? kWave : kWaveDim);
        p.drawLine(x, h / 2 - half, x, h / 2 + half);
    }

    // Zoom indicator. Subtle bottom-right hint when zoomed in so the
    // operator knows the view isn't the whole file. Hidden when
    // showing the full duration.
    if (viewE - viewS < dur - 0.001) {
        const QString hint = tr("zoom %1× (dbl-click to reset)")
            .arg(QString::number(dur / std::max(0.001, viewE - viewS), 'f', 1));
        p.setPen(kTextCol);
        p.drawText(rect().adjusted(0, 0, -6, -4), Qt::AlignBottom | Qt::AlignRight,
                   hint);
    }

    // Trim overlay — full strength when active, ghosted otherwise.
    {
        const bool active = (m_mode == EditMode::Trim);
        const int alpha   = active ? 255 : 90;
        QColor bar = kTrimBar;     bar.setAlpha(alpha);
        QColor grip = kTrimBar;    grip.setAlpha(active ? 255 : 70);

        if (active) {
            QColor scrim(0x0E, 0x0F, 0x12, 160);
            p.fillRect(QRect(0, 0, trimXIn, h), scrim);
            p.fillRect(QRect(trimXOut, 0, w - trimXOut, h), scrim);
        }
        p.setPen(QPen(bar, kHandleWidth));
        p.drawLine(trimXIn,  0, trimXIn,  h);
        p.drawLine(trimXOut, 0, trimXOut, h);
        p.setBrush(grip);
        p.setPen(Qt::NoPen);
        p.drawRect(trimXIn  - 4, h / 2 - 14, 8, 28);
        p.drawRect(trimXOut - 4, h / 2 - 14, 8, 28);
    }

    // Fade overlay — full strength when active, ghosted otherwise.
    {
        const bool active = (m_mode == EditMode::Fade);
        const int xFadeIn  = secondsToPixel(m_fadeIn);
        const int xFadeOut = secondsToPixel(dur - m_fadeOut);

        QColor fill = kFadeFill;
        QColor edge = kFadeEdge;
        if (!active) {
            fill.setAlpha(40);
            edge.setAlpha(110);
        }

        QPainterPath inPath;
        inPath.moveTo(0, h);
        inPath.lineTo(xFadeIn, 0);
        inPath.lineTo(xFadeIn, h);
        inPath.closeSubpath();
        p.fillPath(inPath, fill);
        p.setPen(QPen(edge, kHandleWidth));
        p.drawLine(0, h, xFadeIn, 0);
        p.drawLine(xFadeIn, 0, xFadeIn, h);

        QPainterPath outPath;
        outPath.moveTo(xFadeOut, h);
        outPath.lineTo(xFadeOut, 0);
        outPath.lineTo(w, h);
        outPath.closeSubpath();
        p.fillPath(outPath, fill);
        p.drawLine(xFadeOut, 0, w, h);
        p.drawLine(xFadeOut, 0, xFadeOut, h);

        QColor grip = kFadeEdge;
        grip.setAlpha(active ? 255 : 80);
        p.setBrush(grip);
        p.setPen(Qt::NoPen);
        p.drawRect(xFadeIn  - 4, h / 2 - 14, 8, 28);
        p.drawRect(xFadeOut - 4, h / 2 - 14, 8, 28);
    }

    // Playhead — the live preview voice's current position, in absolute file
    // seconds. Hidden when negative (nothing playing). A bright line with a
    // little top cap so it reads distinctly from the trim/fade bars.
    if (m_playhead >= 0.0) {
        const int px = secondsToPixel(m_playhead);
        if (px >= 0 && px <= w) {
            p.setPen(QPen(QColor(0xF2, 0xF5, 0xF8), 1.5));
            p.drawLine(px, 0, px, h);
            p.setBrush(QColor(0xF2, 0xF5, 0xF8));
            p.setPen(Qt::NoPen);
            p.drawRect(px - 3, 0, 6, 4);
        }
    }
}

void WaveformWidget::setPlayheadSeconds(double sec)
{
    if (m_playhead == sec) return;
    m_playhead = sec;
    update();
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    const int x = event->position().toPoint().x();

    if (event->button() == Qt::MiddleButton) {
        // Middle-drag pan when zoomed in. We capture the view start
        // and the cursor x, then translate cursor delta back into a
        // time delta in mouseMoveEvent. Ignored when fully zoomed out
        // (no point panning a window that already covers everything).
        if (viewSpan() < effectiveDuration() - 0.001) {
            m_panning = true;
            m_panAnchorX = x;
            m_panAnchorViewStart = m_viewStart;
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    m_dragging = hitTest(x);
    if (m_dragging != Handle::None) {
        m_dragPressTime = pixelToSeconds(x);
        switch (m_dragging) {
        case Handle::TrimIn:  m_dragInitialValue = m_trimIn;  break;
        case Handle::TrimOut: m_dragInitialValue = m_trimOut > 0 ? m_trimOut
                                                  : effectiveDuration(); break;
        case Handle::FadeIn:  m_dragInitialValue = m_fadeIn;  break;
        case Handle::FadeOut: m_dragInitialValue = m_fadeOut; break;
        case Handle::None: break;
        }
        setCursor(Qt::SplitHCursor);
    } else {
        // Empty space (or default mode with no handles) — left click/drag
        // scrubs the playhead. Works in every mode; grabbing a trim/fade
        // handle above takes precedence so editing is unaffected.
        m_seeking = true;
        emit seekRequested(pixelToSeconds(x));
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int x = event->position().toPoint().x();

    if (m_panning) {
        const double span = viewSpan();
        const double dx = static_cast<double>(x - m_panAnchorX);
        const double secsPerPx = span / std::max(1, width());
        m_viewStart = m_panAnchorViewStart - dx * secsPerPx;
        m_viewEnd   = m_viewStart + span;
        clampView();
        update();
        return;
    }

    if (m_seeking && (event->buttons() & Qt::LeftButton)) {
        emit seekRequested(pixelToSeconds(x));
        return;
    }

    if (m_dragging == Handle::None) {
        if (m_mode != EditMode::None) {
            setCursor(hitTest(x) != Handle::None ? Qt::SplitHCursor
                                                 : Qt::PointingHandCursor);
        }
        return;
    }

    const double dur = effectiveDuration();

    // Shift = fine-drag: move at 0.1× the cursor delta from the press
    // point. Anchors against m_dragPressTime / m_dragInitialValue so
    // the value glides smoothly even as the cursor wanders far from
    // the original handle position.
    const bool fine = event->modifiers().testFlag(Qt::ShiftModifier);
    const double cursorTime = pixelToSeconds(x);
    const double t = fine
        ? m_dragInitialValue + (cursorTime - m_dragPressTime) * 0.1
        : cursorTime;

    switch (m_dragging) {
    case Handle::TrimIn: {
        const double maxIn = (m_trimOut > 0 ? m_trimOut : dur) - 0.01;
        m_trimIn = std::clamp(t, 0.0, maxIn);
        emit trimInChanged(m_trimIn);
        break;
    }
    case Handle::TrimOut: {
        const double clamped = std::clamp(t, m_trimIn + 0.01, dur);
        m_trimOut = clamped;
        emit trimOutChanged(m_trimOut);
        break;
    }
    case Handle::FadeIn: {
        const double trimSpan = (m_trimOut > 0 ? m_trimOut : dur) - m_trimIn;
        // Fine-drag fadeIn anchors against the duration directly,
        // which matches what the user expects (drag the edge, not
        // the absolute time).
        const double v = fine
            ? m_dragInitialValue + (cursorTime - m_dragPressTime) * 0.1
            : (cursorTime - m_trimIn);
        m_fadeIn = std::clamp(v, 0.0, trimSpan);
        emit fadeInChanged(m_fadeIn);
        break;
    }
    case Handle::FadeOut: {
        const double end = (m_trimOut > 0 ? m_trimOut : dur);
        const double trimSpan = end - m_trimIn;
        const double v = fine
            ? m_dragInitialValue + (m_dragPressTime - cursorTime) * 0.1
            : (end - cursorTime);
        m_fadeOut = std::clamp(v, 0.0, trimSpan);
        emit fadeOutChanged(m_fadeOut);
        break;
    }
    case Handle::None: break;
    }
    update();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        setCursor(m_mode == EditMode::None ? Qt::ArrowCursor
                                            : Qt::PointingHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (m_seeking) {
        // The final position was already emitted on the last press/move.
        m_seeking = false;
        return;
    }
    if (m_dragging != Handle::None) {
        // Snap-to-zero-crossing on trim handles only — fade edges
        // don't audibly benefit (the fade itself smooths the
        // discontinuity).
        if (m_dragging == Handle::TrimIn) {
            const double snapped = snapToZeroCrossing(m_trimIn);
            if (snapped != m_trimIn) {
                m_trimIn = snapped;
                emit trimInChanged(m_trimIn);
            }
        } else if (m_dragging == Handle::TrimOut) {
            const double snapped = snapToZeroCrossing(m_trimOut);
            if (snapped != m_trimOut) {
                m_trimOut = snapped;
                emit trimOutChanged(m_trimOut);
            }
        }
        m_dragging = Handle::None;
        setCursor(m_mode == EditMode::None ? Qt::ArrowCursor
                                            : Qt::PointingHandCursor);
        emit editingFinished();
        update();
    }
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click anywhere resets the zoom to "show full file".
    if (event->button() != Qt::LeftButton) return;
    m_viewStart = 0.0;
    m_viewEnd   = 0.0;
    update();
}

void WaveformWidget::wheelEvent(QWheelEvent *event)
{
    const double dur = effectiveDuration();
    if (dur <= 0.0) { event->ignore(); return; }

    // Wheel zoom toward the cursor: the time under the cursor stays
    // pinned to the same x coordinate after the zoom. Standard pro-
    // audio gesture — Pro Tools, Reaper, Audacity all do this.
    const double zoom = std::pow(1.2, event->angleDelta().y() / 120.0);
    const double oldSpan = viewSpan();
    double newSpan = oldSpan / zoom;
    newSpan = std::clamp(newSpan, 0.005, dur);

    const QPointF pos = event->position();
    const double cursorFrac = std::clamp(pos.x() / std::max(1, width()), 0.0, 1.0);
    const double cursorTime = pixelToSeconds(static_cast<int>(pos.x()));

    m_viewStart = cursorTime - cursorFrac * newSpan;
    m_viewEnd   = m_viewStart + newSpan;
    clampView();
    update();
    event->accept();
}

} // namespace quewi::ui
