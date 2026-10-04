#include "ui/SmoothScroll.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QHeaderView>
#include <QStyleHints>
#include <QTableView>
#include <QTreeView>
#include <QApplication>
#include <QEasingCurve>
#include <QHash>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QVariant>
#include <QWheelEvent>

#include <cmath>

namespace quewi::ui {

namespace {

// 165 Hz = 6 ms / frame. 180 ms = ~30 frames of glide, which is short
// enough to feel responsive (~snappy) but long enough that the eye
// reads the motion as continuous rather than jumpy.
constexpr int   kAnimMs        = 180;
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
        // OutQuart accelerates harder at the start than OutCubic, which
        // makes the wheel feel tighter under the finger. The motion still
        // settles, just faster.
        entry.anim->setEasingCurve(QEasingCurve::OutQuart);
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

namespace {
// The height of the row at the middle of an item view's viewport (tall,
// wrapped rows count as one row each). 0 if it can't tell.
int rowHeightAtMiddle(QAbstractItemView *view)
{
    const QPoint mid(view->viewport()->width() / 2, view->viewport()->height() / 2);
    const QModelIndex i = view->indexAt(mid);
    if (auto *table = qobject_cast<QTableView *>(view)) {
        const int r = i.isValid() ? i.row() : table->rowAt(0);
        return r >= 0 ? table->rowHeight(r) : table->verticalHeader()->defaultSectionSize();
    }
    if (i.isValid()) return view->visualRect(i).height();
    return view->sizeHintForRow(0);
}
} // namespace

int SmoothScroll::stepFor(QAbstractScrollArea *area, QPoint pixelDelta, QPoint angleDelta)
{
    const int px = pixelDelta.y();
    const int angle = angleDelta.y();
    if (px == 0 && angle == 0) return 0;
    const double notches = angle / 120.0;
    auto *view = qobject_cast<QAbstractItemView *>(area);
    const int lines = std::max(1, QApplication::styleHints()->wheelScrollLines());

    // A per-ITEM item view: the scrollbar counts rows.
    if (view && view->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
        if (px != 0) {
            const int h = std::max(1, rowHeightAtMiddle(view));
            const int rows = int(std::lround(double(-px) / h));
            return rows != 0 ? rows : (px > 0 ? -1 : 1);
        }
        const int rows = int(std::lround(-notches * lines));
        return rows != 0 ? rows : (angle > 0 ? -1 : 1);
    }
    if (px != 0) return -px;                       // touchpads: exactly what they say
    // A view that asks for N rows a notch.
    const int wantRows = area->property("smoothScrollRows").toInt();
    if (view && wantRows > 0) {
        const int h = std::max(12, rowHeightAtMiddle(view));
        return int(std::lround(-notches * wantRows * h));
    }
    return int(std::lround(-notches * kPixelStepFallback));
}

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

    const int delta = -stepFor(area, wheel->pixelDelta(), wheel->angleDelta());
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
