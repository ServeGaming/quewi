#pragma once

#include "ui/ShowSnapshot.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QWidget>

#include <functional>
#include <vector>

class QBoxLayout;
class QHBoxLayout;
class QPushButton;
class QTimer;

namespace quewi::ui {

// ── Small building blocks shared with LightingPanel ────────────────────

// A QLabel that elides instead of widening its parent. Long cue names and
// notes are the rule, not the exception, and a show screen must never grow
// a scrollbar or push the GO button off the edge because of one.
class ElideLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElideLabel(QWidget *parent = nullptr);
    // Wrap onto up to `lines` lines before eliding the last one (1 = a
    // single elided line).
    void setMaxLines(int lines);
    int  maxLines() const { return m_maxLines; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_maxLines = 1;
};

// A hairline progress track (2 px radius, theme colours) — the HeliOSC
// "thin progress bar" under a running cue.
//
// The value it draws glides: setProgress() sets a target, and while the
// widget is visible the drawn value eases toward it at 60 fps, running a
// little ahead at the rate the target has been rising so a value that
// arrives in coarse steps (the desk's, every 0.5–1 s) still reads as one
// continuous movement. It snaps instead when the item changes, when the
// value jumps backwards (a restart), becomes unknown, or the bar is hidden —
// so tests that never show the widget see progress() == what they set.
class ThinProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit ThinProgressBar(QWidget *parent = nullptr);
    // 0..1; < 0 = unknown (an empty track). `item` names what's being
    // tracked (a cue id, "list/cue"); a different item snaps, never glides.
    void setProgress(double p, const QString &item = QString());
    double progress() const { return m_target; }        // the value asked for
    double shownProgress() const { return m_shown; }    // the value drawn
    bool   isGliding() const;
    void setColour(const QColor &c);
    void setThickness(int px);

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    void snapTo(double v);
    void frame();

    double  m_target = -1.0;
    double  m_shown  = -1.0;
    QString m_item;
    double  m_rate = -1.0;         // progress per ms between target changes; < 0 = unknown
    double  m_leadMs = 0.0;        // how far past the last change to extrapolate
    QElapsedTimer m_sinceChange;   // since the target last moved
    QElapsedTimer m_sinceFrame;
    QTimer *m_anim = nullptr;
    QColor  m_colour;
    int     m_thickness = 4;
};

// The colour each region of the show screens owns — the header band on its
// card, its heading and its key numbers. Both views use the same set so the
// Lighting dock and Show Mode agree on what blue means.
struct ShowRegionColours {
    QColor standby;   // amber — what GO fires next
    QColor coming;    // neutral — the queue behind it
    QColor running;   // green — playing now
    QColor hits;      // lavender — lighting hits coming up
    QColor desk;      // dusty blue — the desk's own state
};
ShowRegionColours showRegionColours();
QColor showMix(const QColor &a, const QColor &b, double t);   // a→b, t in 0..1

// Text helpers the two views agree on, so "0:03.2" means the same thing
// on the stage-manager screen and in the dock.
QString showClockText(double seconds);                 // "m:ss" (hours when ≥ 1 h)
QString showCountdownText(double seconds);             // "0:03.2"; tenths dropped past 10 min
QString showRemainingText(double seconds);             // "-m:ss"; "" when unknown
QString showDeskLinkText(ShowDeskStatus::Link link);   // "Live", "Connecting"…
QColor  showDeskLinkColour(ShowDeskStatus::Link link);

// ── The stage-manager screen ─────────────────────────────────────────────
//
// Replaces the main window's content while Show Mode is on. Everything it
// shows comes from a ShowSnapshot: either pushed (setSnapshot — tests and
// the poll both use it) or pulled from a provider every 100 ms while the
// view is visible. It never reaches into engines or the workspace; the
// transport buttons only emit signals, and MainWindow owns Space = GO and
// Esc = Panic as window-level shortcuts, so nothing here takes focus.
class ShowModeView : public QWidget {
    Q_OBJECT
public:
    explicit ShowModeView(QWidget *parent = nullptr);

    void setSnapshotProvider(std::function<ShowSnapshot()> provider);
    void setSnapshot(const ShowSnapshot &s);
    const ShowSnapshot &snapshot() const { return m_snap; }

    void setPauseAllowed(bool allowed);
    void setFadeAllAllowed(bool allowed);

    // The show stopwatch (header chip). Click = start/pause, right-click =
    // reset; exposed so tests and MainWindow can drive it.
    void   stopwatchToggle();
    void   stopwatchReset();
    double stopwatchSeconds() const;
    bool   stopwatchRunning() const { return m_swRunning; }

signals:
    void goPressed();
    void pausePressed();
    void fadeAllPressed();
    void panicPressed();
    void exitRequested();
    void armedToggled(bool on);

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void applyScale();
    void tick();
    void render();
    void renderHeader();
    void renderStandby();
    void renderComingUp();
    void renderRunning();
    void renderHits();
    void renderDesk();
    void renderTransport();
    void updateClocks();

    // One "N  Name  notes" row in Coming up / Now playing / the hit list.
    struct Row {
        QWidget    *frame  = nullptr;
        QWidget    *edge   = nullptr;   // the cue's colour, 4 px
        ElideLabel *lead   = nullptr;   // number or countdown
        ElideLabel *title  = nullptr;
        ElideLabel *detail = nullptr;
        ElideLabel *trail  = nullptr;   // remaining / "in 0:04"
        ThinProgressBar *bar = nullptr;
    };
    Row makeRow(QWidget *parent, bool withBar, const char *region);
    void setRowVisible(Row &r, bool on);

    // The coloured header strip across the top of a card: its heading on
    // the left, anything else (a chip, the Armed switch) on the right.
    struct Band {
        QWidget     *frame   = nullptr;
        QLabel      *heading = nullptr;
        QHBoxLayout *lay     = nullptr;
    };
    Band makeBand(QWidget *card, const char *region, const QString &heading);
    std::vector<QHBoxLayout *> m_bandLayouts;

    ShowSnapshot m_snap;
    std::function<ShowSnapshot()> m_provider;
    QTimer *m_tick = nullptr;
    double  m_scale = 1.0;

    bool m_pauseAllowed = true;
    bool m_fadeAllowed  = true;

    // Stopwatch: accumulated seconds plus the running leg.
    QElapsedTimer m_swLeg;
    double m_swBase = 0.0;
    bool   m_swRunning = false;

    // Header
    ElideLabel  *m_showLabel = nullptr;
    ElideLabel  *m_positionLabel = nullptr;
    QPushButton *m_stopwatch = nullptr;
    QLabel      *m_clock = nullptr;
    QPushButton *m_exit = nullptr;

    // Standby hero
    QWidget    *m_standbyCard = nullptr;
    QWidget    *m_standbyEdge = nullptr;
    QLabel     *m_standbyCaps = nullptr;
    QLabel     *m_standbyNumber = nullptr;
    ElideLabel *m_standbyName = nullptr;
    ElideLabel *m_standbyMeta = nullptr;      // type · pre-wait · badges
    ElideLabel *m_standbyNotes = nullptr;
    ElideLabel *m_standbyHits = nullptr;
    QLabel     *m_standbyEmpty = nullptr;

    // Coming up
    QWidget *m_comingCard = nullptr;
    QLabel  *m_comingCaps = nullptr;
    std::vector<Row> m_comingRows;
    QLabel  *m_comingEmpty = nullptr;

    // Now playing
    QWidget *m_runningCard = nullptr;
    QLabel  *m_runningCaps = nullptr;
    std::vector<Row> m_runningRows;
    QLabel  *m_runningState = nullptr;     // "Nothing playing" / "+2 more" / "PAUSED"
    ElideLabel *m_lastFired = nullptr;

    // Next lighting hit
    QWidget     *m_hitsCard = nullptr;
    QLabel      *m_hitsCaps = nullptr;
    QPushButton *m_armed = nullptr;
    QLabel      *m_hitCountdown = nullptr;
    ElideLabel  *m_hitName = nullptr;
    ElideLabel  *m_hitDoes = nullptr;
    std::vector<Row> m_hitRows;
    QLabel      *m_hitsState = nullptr;    // "Triggers disarmed" / "Paused" / quiet text

    // Lighting desk
    QWidget    *m_deskCard = nullptr;
    QLabel     *m_deskCaps = nullptr;
    QLabel     *m_deskDot = nullptr;
    ElideLabel *m_deskLink = nullptr;
    ElideLabel *m_deskDetail = nullptr;
    QLabel     *m_deskActiveCaps = nullptr;
    QLabel     *m_deskActive = nullptr;
    ElideLabel *m_deskActiveLabel = nullptr;
    ThinProgressBar *m_deskBar = nullptr;
    QLabel     *m_deskActiveTime = nullptr;
    QLabel     *m_deskPendingCaps = nullptr;
    ElideLabel *m_deskPending = nullptr;
    QLabel     *m_deskBlind = nullptr;

    // Transport
    QWidget     *m_transport = nullptr;
    QPushButton *m_go = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_fadeAll = nullptr;
    QPushButton *m_panic = nullptr;

    QBoxLayout *m_lowerRow = nullptr;
};

} // namespace quewi::ui
