#pragma once

#include <QJsonArray>
#include <QVariant>

#include <utility>
#include <vector>

namespace quewi::audio {

// Sections cut out of a cue's media: playback jumps from each cut's start to
// its end — the sound (AudioEngine), the picture (VideoLayer) and lighting
// triggers alike. Seconds in the file, the same clock as the In/Out trims;
// kept sorted, non-overlapping and at least 10 ms long. An AudioCue's undoable
// field "cuts" (a video cue forwards it to its soundtrack, like In/Out).
struct Cut {
    double start = 0.0;
    double end   = 0.0;
    double length() const { return end - start; }
    bool operator==(const Cut &o) const { return start == o.start && end == o.end; }
};
using Cuts = std::vector<Cut>;

// Sorted, merged (overlapping or touching cuts become one), tiny ones dropped.
Cuts normalizeCuts(Cuts cuts);

// [[start, end], …] and back. Accepts a QJsonArray, a JSON string or a
// QVariantList; *ok = false on anything unreadable.
QJsonArray cutsToJson(const Cuts &cuts);
Cuts       cutsFromVariant(const QVariant &v, bool *ok = nullptr);

// Add a cut (merging with any it touches) / restore a section (shrinking or
// splitting the cuts it overlaps).
Cuts addCut(const Cuts &cuts, double start, double end);
Cuts removeCut(const Cuts &cuts, double start, double end);

// The cut containing `seconds` (start <= seconds < end), or nullptr.
const Cut *cutAt(const Cuts &cuts, double seconds);

// Seconds of cuts inside [from, to): how much shorter playing that span is.
double cutSecondsBetween(const Cuts &cuts, double from, double to);

} // namespace quewi::audio
