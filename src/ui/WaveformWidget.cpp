#include "ui/WaveformWidget.h"

#include "audio/AudioFile.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

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

int WaveformWidget::secondsToPixel(double sec) const
{
    const double dur = effectiveDuration();
    if (dur <= 0.0) return 0;
    return static_cast<int>((sec / dur) * width());
}

double WaveformWidget::pixelToSeconds(int px) const
{
    const double dur = effectiveDuration();
    if (dur <= 0.0 || width() <= 0) return 0.0;
    return std::clamp(static_cast<double>(px) / width(), 0.0, 1.0) * dur;
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

    // Waveform — dim outside trim window when in trim mode.
    for (int x = 0; x < w; ++x) {
        const int rowStart = static_cast<int>(static_cast<qint64>(x)     * totalRows / w);
        const int rowEnd   = static_cast<int>(static_cast<qint64>(x + 1) * totalRows / w);
        float peak = 0.0f;
        for (int r = rowStart; r < rowEnd && r < totalRows; ++r) {
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

    // Trim mode overlay
    if (m_mode == EditMode::Trim) {
        QColor scrim(0x0E, 0x0F, 0x12, 160);
        p.fillRect(QRect(0, 0, trimXIn, h), scrim);
        p.fillRect(QRect(trimXOut, 0, w - trimXOut, h), scrim);

        p.setPen(QPen(kTrimBar, kHandleWidth));
        p.drawLine(trimXIn,  0, trimXIn,  h);
        p.drawLine(trimXOut, 0, trimXOut, h);

        // Grip handles
        p.setBrush(kTrimBar);
        p.setPen(Qt::NoPen);
        p.drawRect(trimXIn  - 4, h / 2 - 14, 8, 28);
        p.drawRect(trimXOut - 4, h / 2 - 14, 8, 28);
    }

    // Fade mode overlay
    if (m_mode == EditMode::Fade) {
        const int xFadeIn  = secondsToPixel(m_fadeIn);
        const int xFadeOut = secondsToPixel(dur - m_fadeOut);

        QPainterPath inPath;
        inPath.moveTo(0, h);
        inPath.lineTo(0, h);
        inPath.lineTo(xFadeIn, 0);
        inPath.lineTo(xFadeIn, h);
        inPath.closeSubpath();
        p.fillPath(inPath, kFadeFill);
        p.setPen(QPen(kFadeEdge, kHandleWidth));
        p.drawLine(0, h, xFadeIn, 0);
        p.drawLine(xFadeIn, 0, xFadeIn, h);

        QPainterPath outPath;
        outPath.moveTo(xFadeOut, h);
        outPath.lineTo(xFadeOut, 0);
        outPath.lineTo(w, h);
        outPath.lineTo(xFadeOut, h);
        outPath.closeSubpath();
        p.fillPath(outPath, kFadeFill);
        p.drawLine(xFadeOut, 0, w, h);
        p.drawLine(xFadeOut, 0, xFadeOut, h);

        // Grip handles on the inside edge of each fade.
        p.setBrush(kFadeEdge);
        p.setPen(Qt::NoPen);
        p.drawRect(xFadeIn  - 4, h / 2 - 14, 8, 28);
        p.drawRect(xFadeOut - 4, h / 2 - 14, 8, 28);
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    m_dragging = hitTest(event->position().toPoint().x());
    if (m_dragging != Handle::None) {
        setCursor(Qt::SplitHCursor);
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int x = event->position().toPoint().x();
    if (m_dragging == Handle::None) {
        // Hover feedback
        if (m_mode != EditMode::None) {
            setCursor(hitTest(x) != Handle::None ? Qt::SplitHCursor
                                                 : Qt::PointingHandCursor);
        }
        return;
    }

    const double dur = effectiveDuration();
    const double t = pixelToSeconds(x);
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
        m_fadeIn = std::clamp(t - m_trimIn, 0.0, trimSpan);
        emit fadeInChanged(m_fadeIn);
        break;
    }
    case Handle::FadeOut: {
        const double end = (m_trimOut > 0 ? m_trimOut : dur);
        const double trimSpan = end - m_trimIn;
        m_fadeOut = std::clamp(end - t, 0.0, trimSpan);
        emit fadeOutChanged(m_fadeOut);
        break;
    }
    case Handle::None: break;
    }
    update();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_dragging != Handle::None) {
        m_dragging = Handle::None;
        setCursor(m_mode == EditMode::None ? Qt::ArrowCursor
                                            : Qt::PointingHandCursor);
        emit editingFinished();
    }
}

} // namespace quewi::ui
