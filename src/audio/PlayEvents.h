#pragma once

#include <QJsonArray>
#include <QString>
#include <QUuid>
#include <QVariant>

#include <vector>

namespace quewi::audio {

// Things that change how a song plays through, the way a musical theatre
// track player (Concord / ROCS Stage|Tracks) does it: a vamp loops a section
// until the operator presses Advance (or a set number of times, a repeat),
// a hold stops the music at a point until Advance, a skip plays through
// unless Advance is pressed inside it (then it jumps to the skip's end), and
// a marker is just a labelled point. Seconds in the file, the same clock as
// trims, cuts and lighting triggers. An AudioCue's undoable field
// "playEvents".
struct PlayEvent {
    enum class Kind { Vamp, Hold, Skip, Marker };
    // When Advance takes a vamp out: at the end of the pass it's in (the
    // band finishes the vamp, then goes on — the default), at the next bar
    // line (the cue's beat grid; falls back to the end of the pass), or now.
    enum class Exit { EndOfPass, NextBar, Now };

    QUuid   id;
    Kind    kind  = Kind::Vamp;
    QString name;
    double  start = 0.0;
    double  end   = 0.0;     // = start for a hold or a marker
    int     count = 0;       // vamp: 0 = until Advance; N >= 2 = play N times, then go on
    Exit    exit  = Exit::EndOfPass;

    bool isRange() const { return kind == Kind::Vamp || kind == Kind::Skip; }
    bool isRepeat() const { return kind == Kind::Vamp && count > 0; }
    double length() const { return end - start; }
    bool operator==(const PlayEvent &o) const;

    QJsonObject toJson() const;
    static PlayEvent fromJson(const QJsonObject &o, bool *ok = nullptr);

    static QString kindKey(Kind k);           // "vamp", "hold", "skip", "marker"
    static Kind    kindFromKey(const QString &k, bool *ok = nullptr);
    static QString exitKey(Exit e);           // "endOfPass", "nextBar", "now"
    static Exit    exitFromKey(const QString &k);
    // "Vamp", "Repeat ×4", "Hold", "Skip", "Marker".
    QString kindName() const;
};
using PlayEvents = std::vector<PlayEvent>;

// Shortest vamp or skip (a shorter loop is a buzz, not music).
inline constexpr double kMinPlayEventLength = 0.05;

// Sorted by start; vamps / skips shorter than the minimum, events with no
// id, and vamps overlapping an earlier vamp are dropped; ids made up where
// missing.
PlayEvents normalizePlayEvents(PlayEvents events);

QJsonArray playEventsToJson(const PlayEvents &events);
// A QJsonArray, a JSON string or a QVariantList; *ok = false if unreadable.
PlayEvents playEventsFromVariant(const QVariant &v, bool *ok = nullptr);

// The vamp / skip containing `seconds` (start <= s < end), or nullptr.
const PlayEvent *playEventAt(const PlayEvents &events, double seconds, PlayEvent::Kind kind);

} // namespace quewi::audio
