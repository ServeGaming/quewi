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

constexpr int kHeaderW = 96;   // track headers on the left
constexpr int kRulerH  = 24;
constexpr int kLaneGap = 2;
constexpr int kGrab    = 6;    // px either side of a bar / handle
constexpr int kHandleBand = 14;  // top strip of a lane that grabs fade handles
constexpr int kDragStart = 4;    // px of movement before a press becomes a drag
constexpr double kMinSpan = 0.05;  // shortest In→Out, seconds

QString clockText(double s, bool fine)
{
    if (s < 0) s = 0;
    const int m = int(s / 60.0);
    const double rest = s - m * 60.0;
    return fine ? QStringLiteral("%1:%2").arg(m).arg(rest, 5, 'f', 2, QLatin1Char('0'))
                : QStringLiteral("%1:%2").arg(m).arg(int(rest), 2, 10, QLatin1Char('0'));
}

QString lengthText(double s) { return QStringLiteral("%1 s").arg(s, 0, 'f', 2); }

QFont smallCaps(const QFont &base, int px, bool bold)
{
    QFont f = base;
    f.setPixelSize(px);
    f.setBold(bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 112);
    return f;
}

} // namespace

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(minimumSizeHint().height());
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

void VideoTimeline::setFrameRate(double fps) { m_fps = fps > 0.0 ? fps : 0.0; }

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

void VideoTimeline::setCuts(const audio::Cuts &cuts)
{
    if (cuts == m_cuts) return;
    m_cuts = cuts;
    update();
}

double VideoTimeline::endSeconds() const
{
    if (m_out > 0.0 && (m_duration <= 0.0 || m_out < m_duration)) return m_out;
    return m_duration;
}

// ── Tools, selection, split points ─────────────────────────────────────────

void VideoTimeline::setTool(Tool tool)
{
    if (m_tool == tool) return;
    m_tool = tool;
    m_hoverSeconds = -1.0;
    unsetCursor();
    emit toolChanged(m_tool);
    update();
}

void VideoTimeline::setSelection(double start, double end)
{
    if (end < start) std::swap(start, end);
    start = std::max(0.0, start);
    if (m_duration > 0.0) end = std::min(end, m_duration);
    if (end - start < 1e-6) { clearSelection(); return; }
    if (qFuzzyCompare(start + 1.0, m_selStart + 1.0) && qFuzzyCompare(end + 1.0, m_selEnd + 1.0)) return;
    m_selStart = start;
    m_selEnd = end;
    emit selectionChanged();
    update();
}

void VideoTimeline::clearSelection()
{
    if (!hasSelection()) return;
    m_selStart = m_selEnd = 0.0;
    emit selectionChanged();
    update();
}

void VideoTimeline::addSplitPoint(double seconds)
{
    if (m_duration <= 0.0) return;
    seconds = std::clamp(snapToFrame(seconds), 0.0, m_duration);
    const double tol = m_fps > 0.0 ? 0.5 / m_fps : 0.005;
    // Not on In / Out (already boundaries), not twice.
    if (std::abs(seconds - m_in) < tol || std::abs(seconds - endSeconds()) < tol) return;
    for (double s : m_splits) if (std::abs(s - seconds) < tol) return;
    m_splits.append(seconds);
    std::sort(m_splits.begin(), m_splits.end());
    emit splitPointsChanged();
    update();
}

void VideoTimeline::removeSplitPoint(double seconds)
{
    const double tol = m_fps > 0.0 ? 0.5 / m_fps : 0.005;
    for (int i = 0; i < m_splits.size(); ++i) {
        if (std::abs(m_splits[i] - seconds) < tol) {
            m_splits.removeAt(i);
            emit splitPointsChanged();
            update();
            return;
        }
    }
}

void VideoTimeline::clearSplitPoints()
{
    if (m_splits.isEmpty()) return;
    m_splits.clear();
    emit splitPointsChanged();
    update();
}

std::pair<double, double> VideoTimeline::segmentAt(double seconds) const
{
    // Nothing has been split: a click is just a click. Otherwise the split
    // points and the edges of cuts bound the segments, like clips in an NLE.
    if (m_splits.isEmpty() && m_cuts.empty()) return {0.0, 0.0};
    const double end = endSeconds();
    QList<double> bounds{m_in, end};
    for (double s : m_splits) if (s > m_in && s < end) bounds << s;
    for (const auto &c : m_cuts) {
        if (c.start > m_in && c.start < end) bounds << c.start;
        if (c.end > m_in && c.end < end) bounds << c.end;
    }
    std::sort(bounds.begin(), bounds.end());
    for (int i = 0; i + 1 < bounds.size(); ++i)
        if (seconds >= bounds[i] && seconds < bounds[i + 1]) return {bounds[i], bounds[i + 1]};
    return {0.0, 0.0};
}

// ── Geometry ───────────────────────────────────────────────────────────────

QRect VideoTimeline::trackArea() const
{
    return QRect(kHeaderW, 0, std::max(1, width() - kHeaderW), height());
}

QRect VideoTimeline::rulerRect() const
{
    const QRect a = trackArea();
    return QRect(a.left(), 0, a.width(), kRulerH);
}

QRect VideoTimeline::pictureLane() const
{
    const QRect a = trackArea();
    const int avail = std::max(0, height() - kRulerH - kLaneGap - 6);
    const int pic = std::clamp(int(avail * 0.58), 48, 150);
    return QRect(a.left(), kRulerH, a.width(), pic);
}

QRect VideoTimeline::soundLane() const
{
    const QRect a = trackArea();
    const QRect pic = pictureLane();
    const int avail = std::max(0, height() - kRulerH - kLaneGap - 6);
    const int snd = std::clamp(avail - pic.height(), 36, 120);
    return QRect(a.left(), pic.bottom() + 1 + kLaneGap, a.width(), snd);
}

QRect VideoTimeline::lanesRect() const
{
    const QRect pic = pictureLane(), snd = soundLane();
    return QRect(QPoint(pic.left(), pic.top()), QPoint(pic.right(), snd.bottom()));
}

QRect VideoTimeline::muteButtonRect() const
{
    const QRect snd = soundLane();
    return QRect(kHeaderW - 28, snd.top() + (snd.height() - 18) / 2, 18, 18);
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

double VideoTimeline::snapToFrame(double s) const
{
    if (m_fps <= 0.0) return s;
    return std::round(s * m_fps) / m_fps;
}

double VideoTimeline::snapToMarks(double s) const
{
    const QRect a = trackArea();
    if (a.width() <= 0 || viewSpan() <= 0.0) return s;
    const double tol = kGrab * viewSpan() / a.width();
    double best = s, bestDist = tol;
    auto consider = [&](double m) {
        const double d = std::abs(m - s);
        if (d < bestDist) { bestDist = d; best = m; }
    };
    consider(m_in);
    consider(endSeconds());
    if (m_playhead >= 0.0) consider(m_playhead);
    for (double m : m_splits) consider(m);
    for (const auto &c : m_cuts) { consider(c.start); consider(c.end); }
    return best;
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

    paintHeaders(p);

    p.setClipRect(trackArea());
    paintRuler(p);
    paintPicture(p);
    paintSound(p);

    if (m_duration <= 0.0) return;

    const QRect lanes = lanesRect();
    const int top = lanes.top();
    const int bottom = lanes.bottom();
    const int xIn = xForSeconds(m_in);
    const int xOut = xForSeconds(endSeconds());

    // Outside the trims: dimmed, over both lanes.
    const QColor shade(0, 0, 0, 150);
    const QRect area = trackArea();
    if (xIn > area.left())
        p.fillRect(QRect(QPoint(area.left(), top), QPoint(xIn, bottom)), shade);
    if (xOut < area.right())
        p.fillRect(QRect(QPoint(xOut, top), QPoint(area.right(), bottom)), shade);

    paintCuts(p);
    paintMarks(p);

    // In / Out bars with a flag in the ruler.
    auto bar = [&](int x, const QString &label, bool flagRight) {
        p.setPen(QPen(tk.accent, 2));
        p.drawLine(x, kRulerH - 6, x, bottom);
        p.setFont(smallCaps(font(), 9, true));
        const int w = QFontMetrics(p.font()).horizontalAdvance(label) + 8;
        const QRect flag = flagRight ? QRect(x, 3, w, 13) : QRect(x - w, 3, w, 13);
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
        tri.moveTo(x - 5, kRulerH - 10);
        tri.lineTo(x + 5, kRulerH - 10);
        tri.lineTo(x, kRulerH - 3);
        tri.closeSubpath();
        p.fillPath(tri, tk.ink100);
    }

    // The razor's ghost under the mouse.
    if (m_tool == Tool::Razor && m_hoverSeconds >= 0.0) {
        const int x = xForSeconds(m_hoverSeconds);
        QColor ghost = tk.ink100;
        ghost.setAlpha(120);
        p.setPen(QPen(ghost, 1, Qt::DashLine));
        p.drawLine(x, top, x, bottom);
    }
}

void VideoTimeline::paintHeaders(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect pic = pictureLane(), snd = soundLane();
    p.fillRect(QRect(0, 0, kHeaderW, height()), tk.bgPanel);
    p.setPen(tk.divider);
    p.drawLine(kHeaderW - 1, 0, kHeaderW - 1, height());
    p.drawLine(0, kRulerH - 1, kHeaderW - 1, kRulerH - 1);
    p.drawLine(0, pic.bottom() + 1, kHeaderW - 1, pic.bottom() + 1);
    p.drawLine(0, snd.bottom() + 1, kHeaderW - 1, snd.bottom() + 1);

    auto header = [&](const QRect &lane, const QString &id, const QString &name, bool on) {
        p.setFont(smallCaps(font(), 11, true));
        p.setPen(on ? tk.ink100 : tk.ink40);
        const QRect idRect(10, lane.top() + 6, 30, 16);
        p.drawText(idRect, Qt::AlignLeft | Qt::AlignVCenter, id);
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(on ? tk.ink60 : tk.ink40);
        p.drawText(QRect(10, lane.top() + 22, kHeaderW - 20, 14), Qt::AlignLeft | Qt::AlignVCenter, name);
    };
    header(pic, QStringLiteral("V1"), tr("Picture"), true);
    header(snd, QStringLiteral("A1"), tr("Sound"), m_soundEnabled);

    // A1's mute chip: lit (accent) while the sound is off.
    const QRect m = muteButtonRect();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(m_soundEnabled ? tk.outline : tk.accent, 1));
    p.setBrush(m_soundEnabled ? tk.bgInteractive : tk.accent);
    p.drawRoundedRect(m, 3, 3);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(smallCaps(font(), 10, true));
    p.setPen(m_soundEnabled ? tk.ink60 : tk.inkOnAccent);
    p.drawText(m, Qt::AlignCenter, QStringLiteral("M"));
}

void VideoTimeline::paintRuler(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect r = rulerRect();
    p.fillRect(r, tk.bgPanel);
    p.setPen(tk.divider);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    if (m_duration <= 0.0 || r.width() <= 0) return;

    // A tick step that leaves ~90 px between labels, with minor ticks between.
    static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    const double pxPerSec = r.width() / viewSpan();
    double step = steps[std::size(steps) - 1];
    for (double s : steps) if (s * pxPerSec >= 90) { step = s; break; }
    const double minor = step / (step == 0.25 || step == 15 || step == 0.5 ? 5 : (step == 2 ? 4 : 5));

    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const double firstMinor = std::ceil(m_viewStart / minor) * minor;
    p.setPen(tk.divider);
    for (double t = firstMinor; t <= m_viewEnd + 1e-9; t += minor) {
        const int x = xForSeconds(t);
        p.drawLine(x, r.bottom() - 3, x, r.bottom());
    }
    const double first = std::ceil(m_viewStart / step) * step;
    for (double t = first; t <= m_viewEnd + 1e-9; t += step) {
        const int x = xForSeconds(t);
        p.setPen(tk.outline);
        p.drawLine(x, r.bottom() - 7, x, r.bottom());
        p.setPen(tk.ink60);
        p.drawText(QRect(x + 4, r.top(), 80, r.height() - 6), Qt::AlignLeft | Qt::AlignVCenter,
                   clockText(t, step < 1.0));
    }

    // The selection: a band along the ruler's foot with its length.
    if (hasSelection()) {
        const int x0 = xForSeconds(m_selStart), x1 = xForSeconds(m_selEnd);
        QColor band = tk.accent;
        band.setAlpha(110);
        p.fillRect(QRect(x0, r.bottom() - 7, std::max(1, x1 - x0), 7), band);
    }

    // Lighting triggers on this soundtrack: small ticks along the ruler's foot.
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
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
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
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
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

// Cut sections: dark and hatched across both lanes, edged in terracotta,
// with a chip saying how much is gone. Source time stays on the ruler.
void VideoTimeline::paintCuts(QPainter &p)
{
    if (m_cuts.empty()) return;
    const auto &tk = Theme::tokens();
    const QRect lanes = lanesRect();
    QColor hatch = tk.ink40;
    hatch.setAlpha(90);
    QColor edge = tk.err;
    edge.setAlpha(200);
    p.setFont(smallCaps(font(), 9, true));
    const QFontMetrics fm(p.font());
    for (const auto &c : m_cuts) {
        if (c.end < m_viewStart || c.start > m_viewEnd) continue;
        const int x0 = xForSeconds(c.start), x1 = xForSeconds(c.end);
        const QRect r(QPoint(x0, lanes.top()), QPoint(std::max(x0, x1 - 1), lanes.bottom()));
        p.save();
        p.setClipRect(r.intersected(trackArea()));
        p.fillRect(r, QColor(0, 0, 0, 185));
        p.setPen(QPen(hatch, 1));
        for (int x = r.left() - r.height(); x < r.right(); x += 8)
            p.drawLine(x, r.bottom(), x + r.height(), r.top());
        p.restore();
        p.setPen(QPen(edge, 1));
        p.drawLine(x0, lanes.top(), x0, lanes.bottom());
        p.drawLine(x1 - 1, lanes.top(), x1 - 1, lanes.bottom());

        const QString text = tr("CUT  −%1").arg(lengthText(c.length()));
        const int w = fm.horizontalAdvance(text) + 12;
        if (x1 - x0 >= w + 8) {
            const QRect chip((x0 + x1 - w) / 2, pictureLane().center().y() - 9, w, 18);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(tk.err, 1));
            p.setBrush(tk.bgDeep);
            p.drawRoundedRect(chip, 3, 3);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(tk.err);
            p.drawText(chip, Qt::AlignCenter, text);
        }
    }
    p.setBrush(Qt::NoBrush);
}

// The selection (accent wash with edges) and the razor's split points.
void VideoTimeline::paintMarks(QPainter &p)
{
    const auto &tk = Theme::tokens();
    const QRect lanes = lanesRect();

    if (hasSelection()) {
        const int x0 = xForSeconds(m_selStart), x1 = xForSeconds(m_selEnd);
        QColor wash = tk.accent;
        wash.setAlpha(42);
        p.fillRect(QRect(QPoint(x0, lanes.top()), QPoint(std::max(x0, x1 - 1), lanes.bottom())), wash);
        p.setPen(QPen(tk.accent, 1));
        p.drawLine(x0, lanes.top(), x0, lanes.bottom());
        p.drawLine(x1 - 1, lanes.top(), x1 - 1, lanes.bottom());
        p.setFont(smallCaps(font(), 9, true));
        const QString text = lengthText(m_selEnd - m_selStart);
        const int w = QFontMetrics(p.font()).horizontalAdvance(text) + 10;
        if (x1 - x0 >= w + 6) {
            const QRect chip((x0 + x1 - w) / 2, rulerRect().bottom() - 20, w, 13);
            p.fillRect(chip, tk.accent);
            p.setPen(tk.inkOnAccent);
            p.drawText(chip, Qt::AlignCenter, text);
        }
    }

    if (!m_splits.isEmpty()) {
        QColor line = tk.ink100;
        line.setAlpha(170);
        for (double s : m_splits) {
            if (s < m_viewStart || s > m_viewEnd) continue;
            const int x = xForSeconds(s);
            p.setPen(QPen(line, 1, Qt::DashLine));
            p.drawLine(x, lanes.top(), x, lanes.bottom());
            // A notch in the ruler's foot.
            QPainterPath tri;
            tri.moveTo(x - 4, kRulerH - 1);
            tri.lineTo(x + 4, kRulerH - 1);
            tri.lineTo(x, kRulerH - 6);
            tri.closeSubpath();
            p.fillPath(tri, tk.ink60);
        }
    }
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
    if (pos.x() < kHeaderW) {
        if (muteButtonRect().contains(pos)) setCursor(Qt::PointingHandCursor);
        else unsetCursor();
        return;
    }
    if (m_tool == Tool::Razor && lanesRect().contains(pos) && m_duration > 0.0) {
        setCursor(Qt::CrossCursor);
        return;
    }
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
    const QPoint pos = e->position().toPoint();
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panAnchorX = pos.x();
        m_panAnchorStart = m_viewStart;
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (pos.x() >= trackArea().left() && m_duration > 0.0)
            emit contextMenuRequested(std::clamp(secondsForX(pos.x()), 0.0, m_duration),
                                      e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (pos.x() < kHeaderW) {
        if (muteButtonRect().contains(pos)) emit soundMuteToggled(m_soundEnabled);
        return;
    }
    if (m_duration <= 0.0) return;

    m_pressPos = pos;
    m_pressSeconds = secondsForX(pos.x());
    const double t = std::clamp(m_pressSeconds, 0.0, m_duration);

    // The razor drops a split point wherever it clicks in a lane.
    if (m_tool == Tool::Razor) {
        if (lanesRect().contains(pos)) addSplitPoint(t);
        return;
    }

    m_drag = hitTest(pos);
    switch (m_drag) {
    case Handle::TrimIn:     m_dragInitial = m_in; break;
    case Handle::TrimOut:    m_dragInitial = endSeconds(); break;
    case Handle::PicFadeIn:  m_dragInitial = m_in + m_picFadeIn; break;
    case Handle::PicFadeOut: m_dragInitial = endSeconds() - m_picFadeOut; break;
    case Handle::SndFadeIn:  m_dragInitial = m_in + m_sndFadeIn; break;
    case Handle::SndFadeOut: m_dragInitial = endSeconds() - m_sndFadeOut; break;
    case Handle::None:
        if (rulerRect().contains(pos)) {
            m_scrubbing = true;
            emit seekRequested(t);
        } else if (lanesRect().contains(pos)) {
            if ((e->modifiers() & Qt::ShiftModifier) && hasSelection()) {
                // Shift: grow the selection from its far end.
                const double mid = (m_selStart + m_selEnd) / 2;
                m_pressSeconds = t < mid ? m_selEnd : m_selStart;
                m_selecting = true;
                setSelection(std::min(m_pressSeconds, t), std::max(m_pressSeconds, t));
            } else {
                m_pressedInLane = true;
                emit seekRequested(t);
            }
        }
        break;
    }
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const double x = e->position().x();
    if (m_panning) {
        const QRect a = trackArea();
        m_viewStart = m_panAnchorStart - (x - m_panAnchorX) / std::max(1, a.width()) * viewSpan();
        m_viewEnd = m_viewStart + viewSpan();
        clampView();
        update();
        return;
    }
    if (m_scrubbing) {
        emit seekRequested(std::clamp(secondsForX(x), 0.0, m_duration));
        return;
    }
    if (m_pressedInLane && !m_selecting && (pos - m_pressPos).manhattanLength() >= kDragStart) {
        m_selecting = true;
        m_pressedInLane = false;
    }
    if (m_selecting) {
        double t = std::clamp(secondsForX(x), 0.0, m_duration);
        if (!(e->modifiers() & Qt::AltModifier)) t = snapToMarks(snapToFrame(t));
        setSelection(std::min(m_pressSeconds, t), std::max(m_pressSeconds, t));
        return;
    }
    if (m_tool == Tool::Razor) {
        const bool over = lanesRect().contains(pos) && m_duration > 0.0;
        const double ghost = over ? snapToFrame(std::clamp(secondsForX(x), 0.0, m_duration)) : -1.0;
        if (!qFuzzyCompare(ghost + 1.0, m_hoverSeconds + 1.0)) { m_hoverSeconds = ghost; update(); }
    }
    if (m_drag == Handle::None) { updateCursor(pos); return; }
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
    m_scrubbing = false;
    if (m_pressedInLane) {
        // A click, not a drag: pick the segment between split points, if any.
        m_pressedInLane = false;
        const auto seg = segmentAt(std::clamp(m_pressSeconds, 0.0, m_duration));
        if (seg.second > seg.first) setSelection(seg.first, seg.second);
        else clearSelection();
    }
    m_selecting = false;
    if (edited) emit editingFinished();
    updateCursor(e->position().toPoint());
}

void VideoTimeline::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (pos.x() < kHeaderW || m_tool == Tool::Razor) return;
    if (hitTest(pos) == Handle::None && rulerRect().contains(pos)) zoomFit();
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

void VideoTimeline::leaveEvent(QEvent *)
{
    if (m_hoverSeconds >= 0.0) { m_hoverSeconds = -1.0; update(); }
}

} // namespace quewi::ui
