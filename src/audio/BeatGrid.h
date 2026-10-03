#pragma once

#include "audio/LightTrigger.h"

#include <QJsonObject>

#include <cstddef>
#include <vector>

namespace quewi::audio {

// A song's tempo: where its beats fall, so lighting triggers can snap to
// them and a section can be filled with on-the-beat bumps. Times are seconds
// in the song file (the same clock as LightTrigger). Kept on the AudioCue as
// the undoable field "beatGrid"; bpm 0 = no grid.
struct BeatGrid {
    double bpm = 0.0;           // beats per minute; 0 = off
    double firstBeat = 0.0;     // seconds: where beat 1 of bar 1 falls
    int    beatsPerBar = 4;

    bool   isSet() const { return bpm > 0.0; }
    double beatLength() const { return isSet() ? 60.0 / bpm : 0.0; }

    // Beat number at a time (0 = first beat; negative before it; fractional
    // in between) and the time of beat n.
    double beatAt(double seconds) const;
    double timeOfBeat(double beat) const { return firstBeat + beat * beatLength(); }
    bool   isBarLine(long long beat) const;

    // The nearest beat (or, with every = 2/4…, the nearest of every Nth).
    double snap(double seconds, int every = 1) const;

    // Times of every `every`-th beat in [from, to), counted from the first
    // beat (so "every 2" stays on beats 1 and 3, whatever `from` is).
    std::vector<double> beatsIn(double from, double to, int every = 1) const;

    QJsonObject toJson() const;
    static BeatGrid fromJson(const QJsonObject &o);
    bool operator==(const BeatGrid &o) const { return toJson() == o.toJson(); }
};

// Points on the grid in [from, to), each sending `action` — "bump sub 3 on
// every beat of the chorus". Names count up ("Beat 1", "Beat 2"…).
LightTriggers fillWithBeats(const BeatGrid &grid, double from, double to, int every,
                            const TriggerAction &action, const QString &namePrefix);

// Tap tempo: feed it the time of each tap (seconds, any clock); it averages
// the recent intervals. A pause longer than 2 s starts a new count.
class TapTempo {
public:
    // Returns the BPM so far (0 until there are two taps).
    double tap(double seconds);
    double bpm() const { return m_bpm; }
    double lastTap() const { return m_taps.empty() ? 0.0 : m_taps.back(); }
    void   reset() { m_taps.clear(); m_bpm = 0.0; }
private:
    std::vector<double> m_taps;
    double m_bpm = 0.0;
};

// Estimates a song's tempo and first beat from its audio (mono samples).
// Onset energy → autocorrelation over 70–180 BPM → the beat phase with the
// most onsets on it. Good on music with a clear pulse; confidence (0..1) is
// low on rubato or ambient material, where the operator should tap instead.
struct TempoEstimate {
    double bpm = 0.0;
    double firstBeat = 0.0;
    double confidence = 0.0;
};
TempoEstimate estimateTempo(const float *mono, std::size_t frames, int sampleRate);

} // namespace quewi::audio
