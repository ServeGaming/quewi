#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVariant>

#include <vector>

namespace quewi::audio {

// Lighting triggers: points or ranges on an audio cue's song that send
// something to a lighting desk (ETC Eos, grandMA…) when the playhead reaches
// them. Times are seconds in the cue's source file — the same clock as
// AudioEngine::ActiveVoice::positionSeconds — so trimming the cue doesn't
// move them. A video cue's soundtrack is an AudioCue, so it carries them too.
//
// This header is plain data plus the pure crossing logic; GoEngine does the
// sending (it owns the OSC / MIDI engines).

// What one edge of a trigger sends.
struct TriggerAction {
    // Desk = "do this on the show's lighting desk" (core::LightingDesk):
    // the simple mode; audio/DeskCommands turns it into the desk's own OSC
    // or MSC. Osc / Midi / Msc are the expert ("custom") modes.
    enum class Kind { None, Osc, Midi, Msc, FireCue, Desk };
    enum class DeskDo {
        Go,          // GO on the main playback (Eos GO / MA Go+ / MSC GO)
        Stop,        // Eos Stop, MSC STOP
        Back,        // Eos Back, MA Go-
        GoToCue,     // fire cue `number` in list/sequence `list`
        SubLevel,    // Eos: sub `number` to `level` %
        SubBump,     // Eos: press sub `number`'s bump, release after `hold` s
        FaderLevel,  // Eos: fader `number` on page `list` to `level` %
        FaderBump,   // Eos: press fader `number` (page `list`)'s button for `hold` s
        Macro,       // Eos: fire macro `number`
        Command,     // type `text` on the desk's command line (Eos, MA3)
        GoList,      // GO on another cue list: Eos = the GO button of the fader
                     // it's loaded on (fader `number`, page `list`); MA3 =
                     // sequence `list`
    };
    enum class MidiType { NoteOn, NoteOff, ControlChange, ProgramChange, Raw };

    Kind kind = Kind::None;

    // OSC — same arg syntax as an OSC cue: comma-separated, auto-typed
    // (42 → int, 1.5 → float, "text" / text → string, true/false).
    QString host;                 // empty = the lighting desk in Preferences
    int     port = 0;             // 0 = the desk's port
    int     transport = 0;        // 0 UDP, 1 TCP/SLIP, 2 WebSocket
    QString address;
    QString args;

    // MIDI (empty port = first available)
    QString  midiPort;
    MidiType midiType = MidiType::NoteOn;
    int      channel  = 1;        // 1..16
    int      data1    = 60;       // note / controller / program
    int      data2    = 127;      // velocity / value
    QString  rawHex;              // MidiType::Raw: "90 3C 7F"

    // MSC (uses midiPort)
    int     deviceId      = 0x7F; // all-call
    int     commandFormat = 0x01; // Lighting (general)
    int     command       = 0x01; // GO
    QString qNumber;
    QString qList;

    // Fire a cue in the show
    QUuid cueId;

    // Desk (simple mode)
    DeskDo  deskDo = DeskDo::Go;
    QString number;               // cue / sub / fader / macro number ("5", "1.5")
    int     list  = 1;            // cue list / sequence / fader page
    int     level = 100;          // 0..100 %
    double  hold  = 0.25;         // seconds a bump stays pressed
    QString text;                 // command line text

    bool isNone() const { return kind == Kind::None; }
    QByteArray midiBytes() const;   // NoteOn/Off, CC, PC, or parsed raw hex
    QByteArray mscPayload() const;  // Q_number [00 Q_list]
    QString summary() const;        // one line for lists ("OSC /eos/cue/1/fire")

    QJsonObject toJson() const;
    static TriggerAction fromJson(const QJsonObject &o);

    // Dotted-field access for remotes ("address", "port", "kind"…). kind is
    // a key string: none / osc / midi / msc / cue / desk. Desk fields: do
    // (go, stop, back, cue, subLevel, subBump, faderLevel, faderBump, macro,
    // command), number, list, level, hold, text.
    QVariant field(const QString &key) const;
    bool     setField(const QString &key, const QVariant &value);

    static QString kindKey(Kind k);
    static Kind    kindFromKey(const QString &key);
    static QString deskDoKey(DeskDo d);
    static DeskDo  deskDoFromKey(const QString &key);
    static QString deskDoName(DeskDo d);   // "GO (next cue)", "Bump sub", ...
    static QString midiTypeKey(MidiType t);
    static MidiType midiTypeFromKey(const QString &key);

    bool operator==(const TriggerAction &o) const { return toJson() == o.toJson(); }
};

struct LightTrigger {
    QUuid   id = QUuid::createUuid();
    QString name;
    double  start = 0.0;        // seconds in the source file
    double  end   = -1.0;       // > start → a range; otherwise a point
    bool    enabled = true;
    TriggerAction enter;        // a point's only action; a range's start
    TriggerAction exit;         // a range's end (and when it stops inside)

    bool isRange() const { return end > start; }

    QJsonObject toJson() const;
    static LightTrigger fromJson(const QJsonObject &o);

    // Remote field access: name, start, end, enabled, enter, exit (as JSON),
    // and enter.<field> / exit.<field>.
    QVariant field(const QString &key) const;
    bool     setField(const QString &key, const QVariant &value);

    bool operator==(const LightTrigger &o) const { return toJson() == o.toJson(); }
};

using LightTriggers = std::vector<LightTrigger>;

QJsonArray    triggersToJson(const LightTriggers &t);
LightTriggers triggersFromJson(const QJsonArray &a);
// Accepts a QJsonArray, a JSON string, or a QVariantList (what remotes and
// the undo stack hand setField).
LightTriggers triggersFromVariant(const QVariant &v, bool *ok = nullptr);

// One thing to send.
struct TriggerEvent {
    int  index = -1;      // into the triggers vector
    bool exit  = false;   // false = enter / point
    bool operator==(const TriggerEvent &o) const { return index == o.index && exit == o.exit; }
};

// Follows one playing voice and says which triggers it crossed. Pure — feed
// it positions, it returns events — so the timing rules are unit-tested.
//
//  * Moving forward normally: a point fires when crossed; a range sends its
//    enter when the start is crossed and its exit when the end is.
//  * A jump (seek, or more than kMaxStep forward): points in between are
//    skipped, but ranges catch up — landing inside one sends its enter,
//    leaving one sends its exit — so the desk matches the song section.
//  * A loop wrap (the position goes backwards on a looping voice): the tail
//    up to the loop end and the head from the loop start both count as
//    played, so a hit on the loop point isn't lost.
//  * stop(): ranges you were inside send their exit.
class TriggerTracker {
public:
    static constexpr double kMaxStep = 0.5;   // seconds; more = a jump

    // Playback begins at `pos`. A point exactly at `pos` fires; ranges that
    // contain `pos` send their enter.
    std::vector<TriggerEvent> begin(const LightTriggers &t, double pos);

    // The voice is now at `pos`. loopStart/loopEnd describe the loop when
    // the voice loops (loopEnd <= loopStart = not looping). wallElapsed is
    // the real time since the last call, when known: a move that matches it
    // is playback, not a seek, even if the UI thread stalled for a while.
    std::vector<TriggerEvent> advance(const LightTriggers &t, double pos,
                                      double loopStart = 0.0, double loopEnd = -1.0,
                                      double wallElapsed = -1.0);

    // The voice stopped (any reason).
    std::vector<TriggerEvent> stop(const LightTriggers &t);

    double position() const { return m_pos; }

private:
    void crossed(const LightTriggers &t, double from, double to,
                 std::vector<TriggerEvent> &out);       // (from, to]
    void reconcile(const LightTriggers &t, double pos,
                   std::vector<TriggerEvent> &out);     // ranges only
    bool isInside(const QUuid &id) const;
    void setInside(const QUuid &id, bool in);

    double m_pos = 0.0;
    bool   m_started = false;
    std::vector<QUuid> m_inside;    // ranges currently entered
};

} // namespace quewi::audio
