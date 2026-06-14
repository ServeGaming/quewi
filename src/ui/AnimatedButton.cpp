#include "ui/AnimatedButton.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace quewi::ui {

AnimatedButton::AnimatedButton(QWidget *parent) : QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(140);
    m_anim->setEasingCurve(QEasingCurve::OutQuart);
    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) {
                m_hoverProgress = v.toFloat();
                update();
            });
}

AnimatedButton::AnimatedButton(const QString &text, QWidget *parent)
    : AnimatedButton(parent) { setText(text); }

void AnimatedButton::setColors(const QColor &n, const QColor &h,
                               const QColor &pr, const QColor &fg)
{
    m_bgNormal = n; m_bgHover = h; m_bgPressed = pr; m_fgColor = fg;
    update();
}

void AnimatedButton::animateTo(float target)
{
    if (!m_anim) return;
    m_anim->stop();
    m_anim->setStartValue(m_hoverProgress);
    m_anim->setEndValue(target);
    m_anim->start();
}

QColor AnimatedButton::lerp(const QColor &a, const QColor &b, float t) const
{
    t = std::clamp(t, 0.f, 1.f);
    return QColor::fromRgbF(
        a.redF()   * (1.f - t) + b.redF()   * t,
        a.greenF() * (1.f - t) + b.greenF() * t,
        a.blueF()  * (1.f - t) + b.blueF()  * t,
        a.alphaF() * (1.f - t) + b.alphaF() * t);
}

void AnimatedButton::enterEvent(QEnterEvent *e)
{
    QPushButton::enterEvent(e);
    if (isEnabled()) animateTo(1.f);
}

void AnimatedButton::leaveEvent(QEvent *e)
{
    QPushButton::leaveEvent(e);
    animateTo(0.f);
    m_pressed = false;
    update();
}

void AnimatedButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) { m_pressed = true; update(); }
    QPushButton::mousePressEvent(e);
}

void AnimatedButton::mouseReleaseEvent(QMouseEvent *e)
{
    m_pressed = false;
    update();
    QPushButton::mouseReleaseEvent(e);
}

void AnimatedButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg = lerp(m_bgNormal, m_bgHover, m_hoverProgress);
    if (m_pressed) bg = m_bgPressed;
    if (!isEnabled()) bg = m_bgNormal.darker(110);

    // Inset the path by half the pen width so the 1.5 px stroke sits fully
    // inside the widget rect (a hairline 1 px pen on fractional DPI shimmers
    // — the source of the "weird thin border" the buttons used to show).
    const qreal bw = m_useBorder ? 1.5 : 0.0;
    const qreal inset = bw * 0.5;
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                        m_radius, m_radius);
    p.fillPath(path, bg);

    if (m_useBorder) {
        QColor border = isEnabled()
            ? lerp(m_borderColor, m_borderColor.lighter(125), m_hoverProgress)
            : m_borderColor;
        QPen pen(border); pen.setWidthF(bw);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    p.setPen(isEnabled() ? m_fgColor : m_fgColor.darker(140));
    QFont f = font();
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, text());
}

} // namespace quewi::ui
