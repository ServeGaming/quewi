#include "ui/VideoTimeline.h"

#include "audio/AudioFile.h"
#include "ui/Theme.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace quewi::ui {

namespace {

constexpr int kLabelW  = 76;   // lane labels on the left
constexpr int kRulerH  = 22;
constexpr int kPicH    = 72;
constexpr int kSndH    = 56;
constexpr int kLaneGap = 4;
constexpr int kGrab    = 6;    // px either side of a bar / handle
constexpr int kHandleBand = 14;  // top strip of a lane that grabs fade handles
constexpr double kMinSpan = 0.05;  // shortest In→Out, seconds

QString clockText(double s, bool fine)
{
    if (s < 0) s = 0;
    const int m = int(s / 60.0);
    const double rest = s - m * 60.0;
    return fine ? QStringLiteral("%1:%2").arg(m).arg(rest, 5, 'f', 2, QLatin1Char('0'))
                : QStringLiteral("%1:%2").arg(m).arg(int(rest), 2, 10, QLatin1Char('0'));
}

} // namespace

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kRulerH + kPicH + kLaneGap + kSndH + 8);
}

VideoTimeline::~VideoTimeline() = default;

// ── State ──────────────────────────────────────────────────────────────────

void VideoTimeline::setDuration(double seconds)
{
    const bool first = m_duration <= 0.0;
    m_duration = std::max(0.0, seconds);
    if (first || m_viewEnd <= m_viewStart) zoomFit();
    else { clampView(); update(); }
}

void VideoTimeline::setTrims(double inSeconds, double outSeconds)
{
    m_in = std::max(0.0, inSeconds);
    m_out = std::max(0.0, outSeconds);
    update();
}

void VideoTimeline::setPictureFades(double inSeconds, double outSeconds)
{
    m_picFadeIn = std::max(0.0, inSeconds);
    m_picFadeOut = std::max(0.0, outSeconds);
    update();
}

void VideoTimeline::setSoundFades(double inSeconds, double outSeconds)
{
    m_sndFadeIn = std::max(0.0, inSeconds);
    m_sndFadeOut = std::max(0.0, outSeconds);
    update();
}

void VideoTimeline::setSoundEnabled(bool on) { m_soundEnabled = on; update(); }

void VideoTimeline::setAudioFile(std::shared_ptr<audio::AudioFile> file)
{
    if (m_file) disconnect(m_file.get(), nullptr, this, nullptr);
    m_file = std::move(file);
    // Peaks fill in while the file decodes; redraw as they arrive.
    if (m_file)
        connect(m_file.get(), &audio::AudioFile::stateChanged, this, [this] { update(); });
    update();
}

void VideoTimeline::setThumbnail(double atSeconds, const QImage &image)
{
    if (image.isNull()) return;
    m_thumbs[atSeconds] = image;
    update(pictureLane());
}

void VideoTimeline::clearThumbnails() { m_thumbs.clear(); update(); }
void VideoTimeline::setPictureNote(const QString &note) { m_pictureNote = note; update(); }

void VideoTimeline::setPlayhead(double seconds)
{
    if (qFuzzyCompare(m_playhead + 1.0, seconds + 1.0)) return;
    m_playhead = seconds;
    update();
}

void VideoTimeline::setTriggerMarks(const QList<double> &seconds)
{
    m_triggerMarks = seconds;
    update();
}

double VideoTimeline::endSeconds() const
{
    if (m_out > 0.0 && (m_duration <= 0.0 || m_out < m_duration)) return m_out;
    return m_duration;
}

// ── Geometry ───────────────────────────────────────────────────────────────

QRect VideoTimeline::trackArea() const
{
    return QRect(kLabelW, 0, std::max(1, width() - kLabelW - 8), height());
}

QRect VideoTimeline::rulerRect() const
{
    const QRect a = trackArea();
    return QRect(a.left(), 0, a.width(), kRulerH);
}

QRect VideoTimeline::pictureLane() const
{
    const QRect a = trackArea();
    return QRect(a.left(), kRulerH, a.width(), kPicH);
}

QRect VideoTimeline::soundLane() const
{
    const QRect a = trackArea();
    return QRect(a.left(), kRulerH + kPicH + kLaneGap, a.width(), kSndH);
}

int VideoTimeline::xForSeconds(double s) const
{
    const QRect a = trackArea();
    if (viewSpan() <= 0.0) return a.left();
    return a.left() + int(std::lround((s - m_viewStart) / viewSpan() * a.width()));
}

double VideoTimeline::secondsForX(double x) const
{
    const QRect a = trackArea();
    if (a.width() <= 0) return m_viewStart;
    return m_viewStart + (x - a.left()) / a.width() * viewSpan();
}

void VideoTimeline::clampView()
{
    if (m_duration <= 0.0) { m_viewStart = 0.0; m_viewEnd = 1.0; return; }
    double span = std::clamp(viewSpan(), std::min(0.5, m_duration), m_duration);
    m_viewStart = std::clamp(m_viewStart, 0.0, m_duration - span);
    m_viewEnd = m_viewStart + span;
}

void VideoTimeline::zoomFit()
{
    m_viewStart = 0.0;
    m_viewEnd = m_duration > 0.0 ? m_duration : 1.0;
    update();
}

void VideoTimeline::zoomIn()
{
    const double c = m_playhead >= 0 ? m_playhead : (m_viewStart + m_viewEnd) / 2;
    const double span = viewSpan() / 1.5;
    m_viewStart = c - span / 2;
    m_viewEnd = c + span / 2;
    clampView();
    update();
}

void VideoTimeline::zoomOut()
{
    const double c = (m_viewStart + m_viewEnd) / 2;
    const double span = viewSpan() * 1.5;
    m_viewStart = c - span / 2;
    m_viewEnd = c + span / 2;
    clampView();
    update();
}

void VideoTimeline::resizeEvent(QResizeEvent *) { update(); }

// ── Painting ───────────────────────────────────────────────────────────────

void VideoTimeline::paintEvent(QPaintEvent *)
{
    const auto &tk = Theme::tokens();
    QPainter p(this);
    p.fillRect(rect(), tk.bgDeep);

    // Lane labels.
    QFont small = font();
    small.setPixelSize(10);
    small.setBold(true);
    small.setLetterSpacing(QFont::PercentageSpacing, 115);
    p.setFont(small);
    p.setPen(tk.ink40);
    p.drawText(QRect(10, pictureLane().top(), kLabelW - 14, kPicH),
               Qt::AlignLeft | Qt::AlignVCenter, tr("PICTURE"));
    p.setPen(m_soundEnabled ? tk.ink40 : tk.ink40.darker(140));
    p.drawText(QRect(10, soundLane().top(), kLabelW - 14, kSndH),
               Qt::AlignLeft | Qt::AlignVCenter, tr("SOUND"));

    p.setClipRect(trackArea());
    paintRuler(p);
    paintPicture(p);
    paintSound(p);

    if (m_duration <= 0.0) return;

    // Outside the trims: dimmed, over both lanes.
    const int top = pictureLane().top();
    const int bottom = soundLane().bottom();
    const int xIn = xForSeconds(m_in);
    const int xOut = xForSeconds(endSeconds());
    const QColor shade(0, 0, 0, 150);
    const QRect area = trackArea();
    if (xIn > area.left())
        p.fillRect(QRect(QPoint(area.left(), top), QPoint(xIn, bottom)), shade);
    if (xOut < area.right())
        p.fillRect(QRect(QPoint(xOut, top), QPoint(area.right(), bottom)), shade);

    // In / Out bars with a flag in the ruler.
    auto bar = [&](int x, const QString &label, bool flagRight) {
        p.setPen(QPen(tk.accent, 2));
        p.drawLine(x, kRulerH - 6, x, bottom);
        QFont f = font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        const int w = QFontMetrics(f).horizontalAdvance(label) + 8;
        const QRect flag = flagRight ? QRect(x, 2, w, 13) : QRect(x - w, 2, w, 13);
        p.fillRect(flag, tk.accent);
        p.setPen(tk.inkOnAccent);
        p.drawText(flag, Qt::AlignCenter, label);
    };
    bar(xIn, tr("IN"), true);
    bar(xOut, tr("OUT"), false);

    // Playhead.
    if (m_playhead >= 0.0) {
        const int x = xForSeconds(m_playhead);
        p.setPen(QPen(tk.ink100, 1));
        p.drawLine(x, kRulerH - 4, x, bottom);
        QPainterPath tri;
        tri.moveTo(x - 5, kRulerH - 9);
        tri.lineTo(x + 5, kRulerH - 9);
        tri.lineTo(x, kRulerH - 3);
        tri.closeSubpath();
        p.fillPath(tri, tk.ink100);
    }
}

void VideoTimeline::paintRuler(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect r = rulerRect();
    p.fillRect(r, tk.bgPanel);
    p.setPen(tk.divider);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    if (m_duration <= 0.0 || r.width() <= 0) return;

    // A tick step that leaves ~90 px between labels.
    static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    const double pxPerSec = r.width() / viewSpan();
    double step = steps[std::size(steps) - 1];
    for (double s : steps) if (s * pxPerSec >= 90) { step = s; break; }

    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const double first = std::ceil(m_viewStart / step) * step;
    for (double t = first; t <= m_viewEnd + 1e-9; t += step) {
        const int x = xForSeconds(t);
        p.setPen(tk.outline);
        p.drawLine(x, r.bottom() - 6, x, r.bottom());
        p.setPen(tk.ink60);
        p.drawText(QRect(x + 3, r.top(), 80, r.height() - 6), Qt::AlignLeft | Qt::AlignVCenter,
                   clockText(t, step < 1.0));
    }

    // Lighting triggers on this song: small ticks along the ruler's foot.
    p.setPen(QPen(tk.warn, 2));
    for (double t : m_triggerMarks) {
        const int x = xForSeconds(t);
        p.drawLine(x, r.bottom() - 4, x, r.bottom());
    }
}

void VideoTimeline::paintPicture(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect lane = pictureLane();
    p.fillRect(lane, tk.bgRow);

    if (m_thumbs.empty()) {
        p.setPen(tk.ink40);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(lane, Qt::AlignCenter, m_pictureNote);
    } else {
        // Tile thumbnails across the lane, each showing the frame nearest
        // the time under its middle.
        const QImage &any = m_thumbs.begin()->second;
        const double aspect = any.height() > 0 ? double(any.width()) / any.height() : 16.0 / 9.0;
        const int tileW = std::max(24, int(lane.height() * aspect));
        for (int x = lane.left(); x < lane.right(); x += tileW) {
            const double t = secondsForX(x + tileW / 2.0);
            auto it = m_thumbs.lower_bound(t);
            if (it == m_thumbs.end()) it = std::prev(it);
            else if (it != m_thumbs.begin()) {
                auto prev = std::prev(it);
                if (t - prev->first < it->first - t) it = prev;
            }
            p.drawImage(QRect(x, lane.top(), tileW, lane.height()), it->second);
        }
    }
    paintRamp(p, lane, m_picFadeIn, m_picFadeOut);
}

void VideoTimeline::paintSound(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect lane = soundLane();
    p.fillRect(lane, tk.bgRow);
    const int mid = lane.center().y();
    p.setPen(tk.divider);
    p.drawLine(lane.left(), mid, lane.right(), mid);

    const auto *file = m_file.get();
    if (file && file->sampleRate() > 0 && file->channelCount() > 0 && !file->peaks().empty()) {
        const auto &peaks = file->peaks();
        const int ch = file->channelCount();
        const double blockSec = double(audio::AudioFile::kPeakBlock) / file->sampleRate();
        const size_t blocks = peaks.size() / size_t(ch);
        const QColor wave = m_soundEnabled ? tk.ink60 : tk.ink40.darker(150);
        p.setPen(wave);
        const double half = lane.height() / 2.0 - 3;
        for (int x = lane.left(); x <= lane.right(); ++x) {
            const double t0 = secondsForX(x), t1 = secondsForX(x + 1);
            size_t b0 = size_t(std::max(0.0, t0 / blockSec));
            size_t b1 = size_t(std::max(0.0, t1 / blockSec)) + 1;
            if (b0 >= blocks) break;
            b1 = std::min(b1, blocks);
            float peak = 0.0f;
            for (size_t b = b0; b < b1; ++b)
                for (int c = 0; c < ch; ++c)
                    peak = std::max(peak, peaks[b * size_t(ch) + size_t(c)]);
            const int h = int(std::min(1.0f, peak) * half);
            if (h > 0) p.drawLine(x, mid - h, x, mid + h);
        }
    } else {
        p.setPen(tk.ink40);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(lane, Qt::AlignCenter,
                   !m_soundEnabled ? tr("Sound off")
                   : (file && file->state() == audio::AudioFile::State::Failed)
                       ? tr("No soundtrack") : tr("Reading the soundtrack…"));
    }
    if (m_soundEnabled) paintRamp(p, lane, m_sndFadeIn, m_sndFadeOut);
}

void VideoTimeline::paintRamp(QPainter &p, const QRect &lane, double fadeIn, double fadeOut)
{
    if (m_duration <= 0.0) return;
    const auto &tk = Theme::tokens();
    const double end = endSeconds();
    const int xIn = xForSeconds(m_in);
    const int xOut = xForSeconds(end);
    const int xFi = xForSeconds(std::min(end, m_in + fadeIn));
    const int xFo = xForSeconds(std::max(m_in, end - fadeOut));
    const QColor dark(0, 0, 0, 120);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (fadeIn > 0.0) {
        QPolygon tri;
        tri << QPoint(xIn, lane.top()) << QPoint(xFi, lane.top()) << QPoint(xIn, lane.bottom());
        p.setPen(Qt::NoPen);
        p.setBrush(dark);
        p.drawPolygon(tri);
        p.setPen(QPen(tk.accent, 1.5));
        p.drawLine(xIn, lane.bottom(), xFi, lane.top());
    }
    if (fadeOut > 0.0) {
        QPolygon tri;
        tri << QPoint(xFo, lane.top()) << QPoint(xOut, lane.top()) << QPoint(xOut, lane.bottom());
        p.setPen(Qt::NoPen);
        p.setBrush(dark);
        p.drawPolygon(tri);
        p.setPen(QPen(tk.accent, 1.5));
        p.drawLine(xFo, lane.top(), xOut, lane.bottom());
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    // Handles at the ramps' ends (at the In / Out bar when there's no fade).
    p.setPen(Qt::NoPen);
    p.setBrush(tk.accent);
    p.drawRect(QRect(xFi - 4, lane.top(), 8, 8));
    p.drawRect(QRect(xFo - 4, lane.top(), 8, 8));
    p.setBrush(Qt::NoBrush);
}

// ── Interaction ────────────────────────────────────────────────────────────

VideoTimeline::Handle VideoTimeline::hitTest(const QPoint &pos) const
{
    if (m_duration <= 0.0 || pos.x() < trackArea().left()) return Handle::None;
    const double end = endSeconds();
    auto near = [&](double s) { return std::abs(pos.x() - xForSeconds(s)) <= kGrab; };

    // Fade handles first: they live on the lanes' top edge.
    const QRect pic = pictureLane(), snd = soundLane();
    if (pos.y() >= pic.top() && pos.y() < pic.top() + kHandleBand) {
        if (near(std::min(end, m_in + m_picFadeIn)))  return Handle::PicFadeIn;
        if (near(std::max(m_in, end - m_picFadeOut))) return Handle::PicFadeOut;
    }
    if (m_soundEnabled && pos.y() >= snd.top() && pos.y() < snd.top() + kHandleBand) {
        if (near(std::min(end, m_in + m_sndFadeIn)))  return Handle::SndFadeIn;
        if (near(std::max(m_in, end - m_sndFadeOut))) return Handle::SndFadeOut;
    }
    if (pos.y() <= snd.bottom()) {
        if (near(m_in)) return Handle::TrimIn;
        if (near(end))  return Handle::TrimOut;
    }
    return Handle::None;
}

void VideoTimeline::updateCursor(const QPoint &pos)
{
    switch (hitTest(pos)) {
    case Handle::TrimIn: case Handle::TrimOut:
        setCursor(Qt::SizeHorCursor); break;
    case Handle::None:
        unsetCursor(); break;
    default:
        setCursor(Qt::PointingHandCursor); break;
    }
}

void VideoTimeline::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panAnchorX = int(e->position().x());
        m_panAnchorStart = m_viewStart;
        return;
    }
    if (e->button() != Qt::LeftButton || m_duration <= 0.0) return;
    const QPoint pos = e->position().toPoint();
    m_drag = hitTest(pos);
    m_pressSeconds = secondsForX(pos.x());
    switch (m_drag) {
    case Handle::TrimIn:     m_dragInitial = m_in; break;
    case Handle::TrimOut:    m_dragInitial = endSeconds(); break;
    case Handle::PicFadeIn:  m_dragInitial = m_in + m_picFadeIn; break;
    case Handle::PicFadeOut: m_dragInitial = endSeconds() - m_picFadeOut; break;
    case Handle::SndFadeIn:  m_dragInitial = m_in + m_sndFadeIn; break;
    case Handle::SndFadeOut: m_dragInitial = endSeconds() - m_sndFadeOut; break;
    case Handle::None:
        if (pos.x() >= trackArea().left()) {
            m_seeking = true;
            emit seekRequested(std::clamp(m_pressSeconds, 0.0, m_duration));
        }
        break;
    }
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *e)
{
    const double x = e->position().x();
    if (m_panning) {
        const QRect a = trackArea();
        m_viewStart = m_panAnchorStart - (x - m_panAnchorX) / std::max(1, a.width()) * viewSpan();
        m_viewEnd = m_viewStart + viewSpan();
        clampView();
        update();
        return;
    }
    if (m_seeking) {
        emit seekRequested(std::clamp(secondsForX(x), 0.0, m_duration));
        return;
    }
    if (m_drag == Handle::None) { updateCursor(e->position().toPoint()); return; }
    double t = secondsForX(x);
    // Shift: a tenth of the mouse movement, for frame-exact placement.
    if (e->modifiers() & Qt::ShiftModifier)
        t = m_dragInitial + (t - m_pressSeconds) * 0.1;
    applyDrag(t);
}

void VideoTimeline::applyDrag(double t)
{
    const double end = endSeconds();
    switch (m_drag) {
    case Handle::TrimIn: {
        m_in = std::clamp(t, 0.0, std::max(0.0, end - kMinSpan));
        emit trimInChanged(m_in);
        break;
    }
    case Handle::TrimOut: {
        const double out = std::clamp(t, m_in + kMinSpan, m_duration);
        // Dragged back to the end of the file = no Out point.
        m_out = (m_duration - out) < 0.005 ? 0.0 : out;
        emit trimOutChanged(m_out);
        break;
    }
    case Handle::PicFadeIn:
        m_picFadeIn = std::clamp(t - m_in, 0.0, std::max(0.0, end - m_in - m_picFadeOut));
        emit pictureFadeInChanged(m_picFadeIn);
        break;
    case Handle::PicFadeOut:
        m_picFadeOut = std::clamp(end - t, 0.0, std::max(0.0, end - m_in - m_picFadeIn));
        emit pictureFadeOutChanged(m_picFadeOut);
        break;
    case Handle::SndFadeIn:
        m_sndFadeIn = std::clamp(t - m_in, 0.0, std::max(0.0, end - m_in - m_sndFadeOut));
        emit soundFadeInChanged(m_sndFadeIn);
        break;
    case Handle::SndFadeOut:
        m_sndFadeOut = std::clamp(end - t, 0.0, std::max(0.0, end - m_in - m_sndFadeIn));
        emit soundFadeOutChanged(m_sndFadeOut);
        break;
    case Handle::None:
        return;
    }
    update();
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton) { m_panning = false; return; }
    if (e->button() != Qt::LeftButton) return;
    const bool edited = m_drag != Handle::None;
    m_drag = Handle::None;
    m_seeking = false;
    if (edited) emit editingFinished();
    updateCursor(e->position().toPoint());
}

void VideoTimeline::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (hitTest(e->position().toPoint()) == Handle::None) zoomFit();
}

void VideoTimeline::wheelEvent(QWheelEvent *e)
{
    if (m_duration <= 0.0) return;
    const QPoint d = e->angleDelta();
    // Horizontal scroll or Shift+wheel pans; the wheel zooms about the mouse.
    if (d.x() != 0 || (e->modifiers() & Qt::ShiftModifier)) {
        const int delta = d.x() != 0 ? d.x() : d.y();
        m_viewStart -= delta / 120.0 * viewSpan() * 0.1;
        m_viewEnd = m_viewStart + viewSpan();
    } else if (d.y() != 0) {
        const double anchor = secondsForX(e->position().x());
        const double factor = d.y() > 0 ? 1.0 / 1.25 : 1.25;
        m_viewStart = anchor - (anchor - m_viewStart) * factor;
        m_viewEnd = anchor + (m_viewEnd - anchor) * factor;
    }
    clampView();
    update();
    e->accept();
}

} // namespace quewi::ui
