#include "ui/VideoScrubber.h"

#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace quewi::ui {

namespace {
QString fmtTime(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
} // namespace

// ── ScrubTrack — the draggable progress bar with a handle ───────────────
// Modelled on ActiveCuesPanel's SeekBar but with a circular handle and
// real Qt signals (so it composes into VideoScrubber). Drag state is
// exposed so the owner can suppress incoming setPosition while grabbed.
class ScrubTrack : public QWidget {
    Q_OBJECT
public:
    explicit ScrubTrack(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(16);
        setMinimumWidth(120);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

    void setFraction(double f)
    {
        f = std::clamp(f, 0.0, 1.0);
        if (std::abs(f - m_frac) < 1e-4) return;
        m_frac = f;
        update();
    }
    void setActive(bool a)
    {
        if (m_active == a) return;
        m_active = a;
        setCursor(a ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    bool isDragging() const { return m_dragging; }

signals:
    void seekFraction(double f);   // press / drag / release

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &tk = Theme::tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const double cy = height() / 2.0;
        const double trackH = 4.0;
        const double r = 6.0;                 // handle radius
        const double x0 = r;
        const double x1 = width() - r;
        const double usable = std::max(1.0, x1 - x0);

        // Track
        p.setPen(Qt::NoPen);
        p.setBrush(tk.bgRow);
        p.drawRoundedRect(QRectF(x0, cy - trackH / 2, usable, trackH), 2, 2);

        // Filled portion
        const double hx = x0 + m_frac * usable;
        p.setBrush(m_active ? tk.warn : tk.ink40);
        p.drawRoundedRect(QRectF(x0, cy - trackH / 2, hx - x0, trackH), 2, 2);

        // Hover cursor line
        if (m_active && m_hoverX >= 0) {
            QColor hover = tk.ink100;
            hover.setAlpha(180);
            p.setPen(QPen(hover, 1));
            p.drawLine(QPointF(m_hoverX, cy - r), QPointF(m_hoverX, cy + r));
        }

        // Handle
        if (m_active) {
            p.setPen(QPen(tk.ink100, 1.5));
            p.setBrush(tk.accent);
            p.drawEllipse(QPointF(hx, cy), r - 1, r - 1);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (!m_active || e->button() != Qt::LeftButton) return;
        m_dragging = true;
        emitAt(e->position().x());
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        m_hoverX = m_active ? int(e->position().x()) : -1;
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            setFraction(fracAt(e->position().x()));   // optimistic follow
            emitAt(e->position().x());
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_dragging) emitAt(e->position().x());
        m_dragging = false;
    }
    void leaveEvent(QEvent *) override { m_hoverX = -1; update(); }

private:
    double fracAt(double x) const
    {
        const double r = 6.0;
        const double x0 = r, x1 = width() - r;
        return std::clamp((x - x0) / std::max(1.0, x1 - x0), 0.0, 1.0);
    }
    void emitAt(double x) { emit seekFraction(fracAt(x)); }

    double m_frac    = 0.0;
    int    m_hoverX  = -1;
    bool   m_dragging = false;
    bool   m_active  = false;
};

// ── VideoScrubber ───────────────────────────────────────────────────────

VideoScrubber::VideoScrubber(QWidget *parent) : QWidget(parent)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);

    m_play = new QToolButton(this);
    m_play->setAutoRaise(true);
    m_play->setText(QStringLiteral("▶"));   // ▶
    m_play->setToolTip(tr("Play / pause the live video"));
    m_play->setCursor(Qt::PointingHandCursor);

    m_track = new ScrubTrack(this);

    m_time = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    {
        const auto &tk = Theme::tokens();
        m_time->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                  .arg(tk.ink60.name()));
    }
    m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row->addWidget(m_play);
    row->addWidget(m_track, 1);
    row->addWidget(m_time);

    connect(m_play, &QToolButton::clicked, this,
            &VideoScrubber::playPauseRequested);
    connect(m_track, &ScrubTrack::seekFraction, this, [this](double f) {
        if (m_durMs > 0) emit seekRequested(qint64(f * double(m_durMs)));
    });

    setActive(false);
}

void VideoScrubber::setDurationMs(qint64 ms)
{
    if (m_durMs == ms) return;
    m_durMs = ms;
    // A loop or still-decoding clip reports 0 — keep the track inert until
    // a real duration arrives so it isn't misleadingly draggable.
    m_track->setActive(m_active && m_durMs > 0);
    refreshTime();
}

void VideoScrubber::setPositionMs(qint64 ms)
{
    m_posMs = ms;
    if (!m_track->isDragging() && m_durMs > 0)
        m_track->setFraction(double(ms) / double(m_durMs));
    refreshTime();
}

void VideoScrubber::setPlaying(bool playing)
{
    if (m_playing == playing) return;
    m_playing = playing;
    m_play->setText(playing ? QStringLiteral("⏸")   // ⏸
                            : QStringLiteral("▶"));  // ▶
}

void VideoScrubber::setActive(bool hasLiveVoice)
{
    m_active = hasLiveVoice;
    m_play->setEnabled(hasLiveVoice);
    m_track->setActive(hasLiveVoice && m_durMs > 0);
    if (!hasLiveVoice) {
        m_posMs = 0;
        m_durMs = 0;
        m_track->setFraction(0.0);
        m_playing = false;
        m_play->setText(QStringLiteral("▶"));
    }
    refreshTime();
}

void VideoScrubber::refreshTime()
{
    m_time->setText(QStringLiteral("%1 / %2").arg(fmtTime(m_posMs),
                                                  fmtTime(m_durMs)));
}

} // namespace quewi::ui

#include "VideoScrubber.moc"
