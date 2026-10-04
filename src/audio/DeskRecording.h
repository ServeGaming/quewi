#pragma once

#include "audio/BeatGrid.h"
#include "audio/LightTrigger.h"

#include <QString>

#include <vector>

namespace quewi::audio {

// Recording lighting triggers from the desk: while a song plays, every cue
// the lighting operator fires on the desk is noted against the song's
// playhead (seconds in the file — the triggers' own clock), and afterwards
// the take becomes "Go to cue" triggers on that song. Pure: the desk events
// and the playhead are fed in (ui::DeskTakeRecorder does that from the Eos
// read-back link), so the rules are unit-tested.
//
// Recording never sends anything to the desk.
struct RecordedCue {
    double  at = 0.0;           // seconds in the song file
    QString list;               // desk cue list ("" = the desk didn't say)
    QString cue;                // "8.4"
    QString label;
};

class DeskRecording {
public:
    // `list` = only cues from that desk list ("" = any list).
    void start(const QString &list = {});
    void stop() { m_recording = false; }
    bool isRecording() const { return m_recording; }
    void clear() { m_takes.clear(); }

    // A cue the desk says just ran, at `at` seconds in the song. Ignored when
    // not recording, for another list, before the song (at < 0), or when the
    // same cue was noted less than kSameCueWindow ago (the desk reports one
    // fire more than one way). Returns whether it was kept.
    static constexpr double kSameCueWindow = 0.3;
    bool add(const QString &list, const QString &cue, const QString &label, double at);

    const std::vector<RecordedCue> &takes() const { return m_takes; }

    // The take as point triggers ("Go to cue <cue> (list <list>)"), named
    // "LX <cue> <label>", in time order; `snap` (a set grid) moves each onto
    // the nearest beat. `group` names them all (empty = no group).
    static LightTriggers toTriggers(const std::vector<RecordedCue> &takes, const QString &group = {},
                                    const BeatGrid *snap = nullptr);

    // "0:12.40" — a take's time as the review list shows it.
    static QString timeText(double seconds);

private:
    bool m_recording = false;
    QString m_list;
    std::vector<RecordedCue> m_takes;
};

} // namespace quewi::audio
