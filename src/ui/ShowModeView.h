#pragma once

#include "ui/ShowSnapshot.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QWidget>

#include <functional>

class QBoxLayout;
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
class ThinProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit ThinProgressBar(QWidget *parent = nullptr);
    // 0..1; < 0 = unknown (an empty track).
    void setProgress(double p);
    double progress() const { return m_progress; }
    void setColour(const QColor &c);
    void setThickness(int px);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_progress = -1.0;
    QColor m_colour;
    int    m_thickness = 4;
};

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
    Row makeRow(QWidget *parent, bool withBar);
    void setRowVisible(Row &r, bool on);

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
