#pragma once

#include <QObject>

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
class SmoothScroll : public QObject {
    Q_OBJECT
public:
    static void install(QObject *appOrParent);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit SmoothScroll(QObject *parent);
};

} // namespace quewi::ui
