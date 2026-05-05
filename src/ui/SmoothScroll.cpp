#include "ui/SmoothScroll.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEasingCurve>
#include <QHash>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QVariant>
#include <QWheelEvent>

namespace quewi::ui {

namespace {

constexpr int   kAnimMs        = 220;
constexpr int   kPixelStepFallback = 60; // for devices reporting only angleDelta

// One animation per scrollbar — re-targeted on each wheel tick rather
// than queued, so fast rolls feel like a continuous glide rather than a
// staircase of overlapping tweens.
struct AnimEntry {
    QPointer<QScrollBar>      bar;
    QPointer<QPropertyAnimation> anim;
};
QHash<QScrollBar *, AnimEntry> g_anims;

void animateBarTo(QScrollBar *bar, int target)
{
    if (!bar) return;
    target = qBound(bar->minimum(), target, bar->maximum());
    auto &entry = g_anims[bar];
    if (!entry.anim) {
        entry.bar  = bar;
        entry.anim = new QPropertyAnimation(bar, "value");
        entry.anim->setEasingCurve(QEasingCurve::OutCubic);
        entry.anim->setDuration(kAnimMs);
        QObject::connect(bar, &QObject::destroyed, [bar] { g_anims.remove(bar); });
    }
    // Re-target: start from the bar's *current* value (not the previous
    // animation's start), so a scroll mid-tween blends smoothly.
    entry.anim->stop();
    entry.anim->setStartValue(bar->value());
    entry.anim->setEndValue(target);
    entry.anim->start();
}

bool optedOut(QObject *obj)
{
    for (QObject *o = obj; o; o = o->parent()) {
        const auto v = o->property("smoothScroll");
        if (v.isValid() && !v.toBool()) return true;
    }
    return false;
}

} // namespace

SmoothScroll::SmoothScroll(QObject *parent) : QObject(parent) {}

void SmoothScroll::install(QObject *appOrParent)
{
    static QPointer<SmoothScroll> instance;
    if (instance) return;
    instance = new SmoothScroll(appOrParent);
    if (auto *app = qApp) app->installEventFilter(instance);
}

bool SmoothScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel) return false;
    auto *wheel = static_cast<QWheelEvent *>(event);

    // Only handle vertical wheel deltas without modifiers — Ctrl is
    // typically zoom, Shift is horizontal nudge, both belong to the
    // target widget.
    if (wheel->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier
                              | Qt::AltModifier  | Qt::MetaModifier))
        return false;

    auto *area = qobject_cast<QAbstractScrollArea *>(watched);
    if (!area) {
        // The wheel event might have arrived at a child widget (e.g. the
        // viewport). Walk up to find the scroll area.
        QObject *p = watched;
        while (p && !(area = qobject_cast<QAbstractScrollArea *>(p))) p = p->parent();
        if (!area) return false;
    }
    if (optedOut(area)) return false;

    auto *bar = area->verticalScrollBar();
    if (!bar || !bar->isVisible()) return false;

    int delta = wheel->pixelDelta().y();
    if (delta == 0) {
        // Mouse wheels report angleDelta in 1/8 degrees; 120 = one notch.
        const int angle = wheel->angleDelta().y();
        delta = angle != 0 ? angle * kPixelStepFallback / 120 : 0;
    }
    if (delta == 0) return false;

    // Animate from the *current animation target* if one is running, so
    // repeated ticks accumulate into one longer glide.
    int from = bar->value();
    if (auto it = g_anims.constFind(bar); it != g_anims.constEnd() && it->anim
        && it->anim->state() == QAbstractAnimation::Running) {
        from = it->anim->endValue().toInt();
    }
    animateBarTo(bar, from - delta);
    event->accept();
    return true;
}

} // namespace quewi::ui
