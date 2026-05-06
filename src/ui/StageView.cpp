#include "ui/StageView.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace quewi::ui {

namespace {
constexpr float kPi = 3.14159265358979323846f;
inline float deg2rad(float d) { return d * (kPi / 180.0f); }
inline float rad2deg(float r) { return r * (180.0f / kPi); }

// Map azimuth/elevation to a 2D unit-disc point.
//   azimuth 0°  → +Y (front)        — drawn upward
//   azimuth 90° → +X (right)
//   elevation lifts the source toward the centre (overhead = origin).
QPointF projectToDisc(float azDeg, float elDeg)
{
    const float az = deg2rad(azDeg);
    const float el = deg2rad(elDeg);
    const float r  = std::cos(el);              // 1 at ear-level, 0 overhead
    return { std::sin(az) * r, -std::cos(az) * r };
}
} // namespace

StageView::StageView(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMinimumSize(minimumSizeHint());
    setMouseTracking(false);
    setCursor(Qt::CrossCursor);
}

void StageView::setSpeakers(const QList<audio::Speaker> &speakers)
{
    m_speakers = speakers;
    update();
}

void StageView::setAzimuth(float deg)
{
    if (qFuzzyCompare(m_azimuth + 1.0f, deg + 1.0f)) return;
    m_azimuth = deg;
    update();
}

void StageView::setElevation(float deg)
{
    if (qFuzzyCompare(m_elevation + 1.0f, deg + 1.0f)) return;
    m_elevation = deg;
    update();
}

QPointF StageView::stageCenter() const
{
    return { width() * 0.5, height() * 0.5 };
}

qreal StageView::stageRadius() const
{
    return std::max<qreal>(8.0, std::min(width(), height()) * 0.5 - 18.0);
}

QPointF StageView::positionToPoint(float az, float el) const
{
    const auto disc = projectToDisc(az, el);
    const qreal r = stageRadius();
    const auto c  = stageCenter();
    return { c.x() + disc.x() * r, c.y() + disc.y() * r };
}

void StageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c = stageCenter();
    const qreal   R = stageRadius();

    const QColor ringCol   = palette().color(QPalette::Mid);
    const QColor faintCol  = palette().color(QPalette::Midlight);
    const QColor textCol   = palette().color(QPalette::Text);
    const QColor accentCol = palette().color(QPalette::Highlight);

    // Stage disc.
    p.setPen(QPen(ringCol, 1.2));
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(c, R, R);

    // Inner ring at elevation 45° (cosine = √½).
    p.setPen(QPen(faintCol, 1, Qt::DotLine));
    p.setBrush(Qt::NoBrush);
    const qreal r45 = R * std::cos(deg2rad(45.0f));
    p.drawEllipse(c, r45, r45);

    // Crosshair.
    p.setPen(QPen(faintCol, 1, Qt::DotLine));
    p.drawLine(QPointF(c.x() - R, c.y()), QPointF(c.x() + R, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - R), QPointF(c.x(), c.y() + R));

    // Front-of-stage marker.
    p.setPen(QPen(textCol, 1));
    auto fm = QFontMetrics(font());
    const int frontW = fm.horizontalAdvance(QStringLiteral("FRONT"));
    p.drawText(QPointF(c.x() - frontW * 0.5,
                        c.y() - R - 6),
               QStringLiteral("FRONT"));

    // Speakers.
    for (const auto &s : m_speakers) {
        const QPointF pt = positionToPoint(s.azimuthDeg, s.elevationDeg);
        const bool elevated = std::abs(s.elevationDeg) > 5.0f;
        QColor box = elevated ? accentCol : ringCol;
        if (elevated) box.setAlpha(220);
        p.setBrush(box);
        p.setPen(QPen(textCol, 1));
        const QRectF r(pt.x() - 5, pt.y() - 5, 10, 10);
        p.drawRect(r);
        // Channel label below the speaker.
        p.setPen(textCol);
        const auto label = QString::number(s.channel);
        const int  lw    = fm.horizontalAdvance(label);
        p.drawText(QPointF(pt.x() - lw * 0.5, pt.y() + 18), label);
    }

    // Source (draggable).
    const QPointF src = positionToPoint(m_azimuth, m_elevation);
    p.setPen(Qt::NoPen);
    QColor halo = accentCol; halo.setAlpha(50);
    p.setBrush(halo);
    p.drawEllipse(src, 14, 14);
    p.setBrush(accentCol);
    p.drawEllipse(src, 7, 7);

    // Numeric readout in the corner.
    p.setPen(textCol);
    const auto txt = QStringLiteral("Az %1°  El %2°")
        .arg(QString::number(double(m_azimuth),   'f', 0))
        .arg(QString::number(double(m_elevation), 'f', 0));
    p.drawText(QPointF(8, height() - 8), txt);
}

void StageView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_dragging = true;
    updateFromPoint(e->position());
}

void StageView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) return;
    updateFromPoint(e->position());
}

void StageView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_dragging = false;
}

void StageView::updateFromPoint(const QPointF &p)
{
    const QPointF c = stageCenter();
    const qreal   R = stageRadius();
    qreal dx = p.x() - c.x();
    qreal dy = p.y() - c.y();
    qreal len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3) {
        // Click on the centre = directly overhead.
        m_azimuth   = 0.0f;
        m_elevation = 90.0f;
        emit positionChanged(m_azimuth, m_elevation);
        update();
        return;
    }
    const qreal r = std::min<qreal>(1.0, len / R);
    m_azimuth   = rad2deg(std::atan2(static_cast<float>(dx), -static_cast<float>(dy)));
    m_elevation = rad2deg(std::acos(std::clamp(static_cast<float>(r), 0.0f, 1.0f)));
    if (m_azimuth   >  180.0f) m_azimuth   -= 360.0f;
    if (m_azimuth   < -180.0f) m_azimuth   += 360.0f;
    if (m_elevation < 0.0f)    m_elevation  = 0.0f;

    emit positionChanged(m_azimuth, m_elevation);
    update();
}

} // namespace quewi::ui
