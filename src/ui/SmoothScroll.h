#pragma once

#include <QObject>
#include <QPoint>

class QAbstractScrollArea;

namespace quewi::ui {

// Application-wide smooth scrolling. Installed once on QApplication; it
// intercepts QWheelEvents headed for any QAbstractScrollArea subclass and
// replaces the discrete scrollbar jump with an eased animation that
// stacks gracefully on rapid wheel ticks.
//
// Why an event filter and not a per-widget subclass: every list / tree /
// scroll area in the app should benefit (cue list, inspector,
// preferences, OSC monitor, spectrogram, …) without touching each one.
//
// Not installed on widgets that opt out by setting the dynamic property
// `smoothScroll` to false — useful for places where the snap is part of
// the UX (e.g. the timeline ruler when Shift+wheel = horizontal nudge).
//
// How far a notch goes: 60 px, or — on an item view whose dynamic property
// `smoothScrollRows` is set — that many rows of the view (measured at the
// middle of the viewport, so tall rows still move N rows). Item views left
// in ScrollPerItem mode have a scrollbar counted in ROWS, not pixels; a notch
// there moves the system's wheel-lines (3) rows rather than "60". Touchpads
// that report pixelDelta scroll by exactly those pixels (or the matching
// fraction of a row, per-item).
//
// stepFor() is the pure arithmetic, exposed for tests.
class SmoothScroll : public QObject {
    Q_OBJECT
public:
    static void install(QObject *appOrParent);

    bool eventFilter(QObject *watched, QEvent *event) override;

    // How much the scrollbar of `area` moves for this wheel (scrollbar
    // units, positive = down). 0 = not ours to handle.
    static int stepFor(QAbstractScrollArea *area, QPoint pixelDelta, QPoint angleDelta);

private:
    explicit SmoothScroll(QObject *parent);
};

} // namespace quewi::ui
