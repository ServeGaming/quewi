#pragma once

#include <cmath>

namespace quewi::audio {

// Decibel ↔ linear-amplitude conversions — the single source of truth
// for the formula used throughout the audio path (mixer gain, fade
// engine, the offline renderer, GoEngine's level cues, and the
// effects' make-up gain). Previously this `std::pow(10, db/20)` was
// re-inlined in five places; a divergence in the constant would silently
// park levels at the wrong gain mid-show, so it lives here and is unit
// tested (tests/test_fade.cpp).
//
// Amplitude (not power) convention: −6 dB ≈ 0.501, 0 dB = 1.0,
// +6 dB ≈ 1.995. The −90 dB floor on the inverse keeps a digital-silent
// signal from reporting −inf in meters.

inline double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

inline double linearToDb(double lin)
{
    if (lin <= 1e-9) return -90.0;
    return 20.0 * std::log10(lin);
}

} // namespace quewi::audio
