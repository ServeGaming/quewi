#pragma once

#include "audio/Cuts.h"

#include <algorithm>

namespace quewi::video {

// When a video cue's picture starts, stops and fades, in seconds of the
// file. Pure arithmetic, shared by the playing layer (VideoLayer) and the
// video editor's preview so both show the same thing.
//
// The In/Out points are the cue's sound trims (VideoCue keeps them as one
// value) so picture and soundtrack can't drift apart; the fades are the
// picture's own — an opacity ramp up from the In point and down to the Out.
struct PictureTiming {
    double inSeconds      = 0.0;   // start this far into the file
    double outSeconds     = 0.0;   // stop here; 0 = the end of the file
    double fadeInSeconds  = 0.0;   // picture opacity ramp after In
    double fadeOutSeconds = 0.0;   // …and before Out
    bool   loop           = false; // loop between In and Out
    audio::Cuts cuts;              // sections jumped over (the sound's too)

    // Where to continue from when the picture is inside a cut: the cut's
    // end; otherwise posSeconds itself.
    double skipCuts(double posSeconds) const
    {
        const auto *c = audio::cutAt(cuts, posSeconds);
        return c ? c->end : posSeconds;
    }
    // How long the cue plays between In and the end: trims and cuts applied.
    double playSeconds(double durationSeconds) const
    {
        const double end = endSeconds(durationSeconds);
        if (end <= inSeconds) return 0.0;
        return (end - inSeconds) - audio::cutSecondsBetween(cuts, inSeconds, end);
    }

    // Where the picture stops (or wraps): Out, or the end of the file.
    // durationSeconds <= 0 (not known yet) leaves Out as given.
    double endSeconds(double durationSeconds) const
    {
        if (outSeconds > 0.0 && (durationSeconds <= 0.0 || outSeconds < durationSeconds))
            return outSeconds;
        return durationSeconds;
    }

    // Anything that needs the player to start somewhere other than the top
    // or stop before the end.
    bool isTrimmed() const { return inSeconds > 0.0 || outSeconds > 0.0; }

    // Opacity multiplier (0..1) at file position posSeconds. The fade in
    // runs on the first pass only; the fade out never runs while looping
    // (a loop has no last pass). An unknown duration skips the fade out.
    double envelope(double posSeconds, double durationSeconds, bool firstPass) const
    {
        double e = 1.0;
        if (firstPass && fadeInSeconds > 0.0)
            e = std::min(e, (posSeconds - inSeconds) / fadeInSeconds);
        const double end = endSeconds(durationSeconds);
        if (!loop && fadeOutSeconds > 0.0 && end > 0.0)
            e = std::min(e, (end - posSeconds) / fadeOutSeconds);
        return std::clamp(e, 0.0, 1.0);
    }
};

} // namespace quewi::video
