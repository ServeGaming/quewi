#pragma once

#include <QColor>
#include <QString>
#include <QUuid>

#include <optional>
#include <vector>

namespace quewi::ui {

// Everything the stage-manager screens show, as plain data. MainWindow builds
// one on demand (ShowModeView and LightingPanel poll a provider ~10 times a
// second), so the views never reach into the engines, and tests can feed them
// any state they like.

// One cue, as a stage manager reads it.
struct ShowCueLine {
    QUuid   id;
    QString number;            // "12" or "12.5" — as typed, no trailing zeros
    QString name;              // falls back to the type name when unnamed
    QString type;              // "Audio", "Video", "Light"…
    QString notes;             // the cue's notes (the SM's script line / standby call)
    QColor  colour;            // the cue's colour; invalid = none
    double  preWaitSeconds  = 0.0;
    double  durationSeconds = 0.0;   // 0 = none / unknown (memo, OSC…)
    bool    autoContinue = false;    // GO also fires the next (after its pre-wait)
    bool    autoFollow   = false;    // the next fires when this one ends
    int     lightTriggers = 0;       // how many lighting triggers its song has
    double  firstTriggerSeconds = -1.0;   // first one, seconds after GO; < 0 = none
    // Matrix List only (empty / false otherwise): the desk cues that go with
    // this cue ("LX 12 Sunrise · LX 12.5"), and whether this line is a desk
    // cue on its own (then number = "LX 14", name = its label).
    QString deskCues;
    bool    deskOnly = false;
};

// A cue that's playing now.
struct ShowRunningCue {
    ShowCueLine cue;
    double elapsedSeconds   = 0.0;
    double remainingSeconds = -1.0;  // < 0 = unknown (looping, no length)
    double progress = -1.0;          // 0..1; < 0 = unknown
    bool   paused   = false;
    bool   looping  = false;
};

// A lighting trigger coming up in a playing song.
struct ShowUpcomingHit {
    QUuid   triggerId;
    QString cueNumber;          // the song it's in
    QString cueName;
    QString name;               // the trigger's name
    QString does;               // "Desk: GO", "Desk: bump sub 3"…
    double  inSeconds = 0.0;    // from now
    bool    exit   = false;     // a range's end
    bool    paused = false;     // its song is paused (the countdown is frozen)
};

// The lighting desk's own state, read back from it (Eos only, over TCP).
struct ShowDeskStatus {
    enum class Link {
        Off,            // no feedback for this desk type, or turned off
        Connecting,     // trying / reconnecting
        Live,           // talking
        Failed,         // can't reach it (see detail)
    };
    Link    link = Link::Off;
    QString deskName;           // "ETC Eos / Ion / Element / Nomad"
    QString address;            // "127.0.0.1:3032"
    QString detail;             // why it failed / what it's doing
    QString showName;           // the desk's show file, when it says
    // Active (running) and pending (next) cue on the desk's main list.
    QString activeList, activeCue, activeLabel;
    double  activeProgress = -1.0;  // 0..1; < 0 = unknown
    QString activeTime;             // "5.00" as the desk wrote it
    QString pendingList, pendingCue, pendingLabel;
    bool    blind = false;          // the desk is in Blind
};

// GO Lights (the Matrix List's button for the desk's next cue), when the
// Matrix List is the page the operator was on.
struct ShowLightsGo {
    bool    enabled = false;
    QString text;               // "GO Lights  8.4  Back"
    QString reason;             // why it's disabled / what it will fire
    // Back / Stop for the lights, beside it.
    bool    backEnabled = false;
    QString backText;           // "◀ Back  6"
    QString backReason;
    bool    stopEnabled = false;
    QString stopReason;
};

struct ShowSnapshot {
    QString showName;           // quewi's show (file name)
    QString listName;           // the cue list GO runs
    int     position = 0;       // standby's 1-based position in the list (0 = end)
    int     cueCount = 0;

    std::optional<ShowCueLine> standby;      // what GO fires next
    std::vector<ShowCueLine>   comingUp;     // the few after standby, in order
                                             // (a Matrix List: quewi and desk cues merged)
    std::optional<ShowCueLine> lastFired;    // the most recent GO
    std::vector<ShowRunningCue> running;     // playing now, newest first

    bool paused = false;        // everything paused (Pause)
    bool triggersArmed = true;
    std::vector<ShowUpcomingHit> hits;       // soonest first
    ShowDeskStatus desk;
    std::optional<ShowLightsGo> lightsGo;     // set only when the Matrix List is up
};

} // namespace quewi::ui
