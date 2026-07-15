#pragma once

#include <QString>
#include <QStringView>
#include <QVector>
#include <optional>

// Value encodings for the Behringer X32 / Midas M32 OSC protocol.
//
// Pure functions, no I/O — everything here is unit-tested against the
// worked examples in the protocol documentation. See
// docs/dev/console-protocols.md for sourcing; the traps encoded here are
// the reason this file exists separately from the link.
//
// Every float on the X32 wire is [0.0, 1.0]. The console recognises only a
// finite grid of discrete values and ROUNDS out-of-grid floats rather than
// rejecting them, so a wrong mapping fails silently — hence the tests.
namespace quewi::mix::x32 {

// ── DCA membership ───────────────────────────────────────────────────
//
// /ch/NN/grp/dca carries an 8-bit mask: DCA n is bit (n-1). DCA1 is bit 0
// (value 1); DCA8 is bit 7 (value 128).
//
// The protocol doc says only "8 bits bitmap" and never states which bit is
// DCA1 — a 50/50 guess that would silently mute the wrong cast members.
// Verified against real production scene files: channels named "Runner 1..4"
// carry %00000010 in a show whose /dca/2/config is "Run". MSB-first reads
// DCA2 and holds across 22 channels; LSB-first reads DCA7 and is nonsense.
//
// NOTE: grp/dca is replace-not-toggle. There is no per-membership address,
// so callers must read-modify-write against cached state.
inline constexpr int kDcaCount     = 8;
inline constexpr int kChannelCount = 32;   // identical on every X32/M32 variant

using DcaMask = quint8;

// Returns 0 for out-of-range dca rather than shifting UB.
constexpr DcaMask dcaBit(int dca)
{
    return (dca >= 1 && dca <= kDcaCount) ? DcaMask(1u << (dca - 1)) : DcaMask(0);
}

constexpr bool    dcaMaskContains(DcaMask mask, int dca) { return (mask & dcaBit(dca)) != 0; }
constexpr DcaMask dcaMaskWith(DcaMask mask, int dca)     { return DcaMask(mask | dcaBit(dca)); }
constexpr DcaMask dcaMaskWithout(DcaMask mask, int dca)  { return DcaMask(mask & ~dcaBit(dca)); }

// 1-based DCA numbers, ascending. Out-of-range entries in the input are
// ignored (dcaBit yields 0), so a bad list can't corrupt the mask.
QVector<int> dcaMaskToList(DcaMask mask);
DcaMask      dcaListToMask(const QVector<int> &dcas);

// Parse the node-text form, e.g. "%00000010" -> 2. Plain MSB-first binary.
// Tolerates the emulator's leading-zero-stripped form ("%10") as well as the
// real console's fixed width. Returns nullopt on anything unparseable.
std::optional<DcaMask> parseMaskText(QStringView text);

// Render fixed-width like the console does: 2 -> "%00000010".
QString maskText(DcaMask mask, int width = 8);

// ── Addresses ────────────────────────────────────────────────────────
//
// Three different zero-pad widths coexist in this protocol and mixing them
// up is a documented, common bug:
//     /ch/01        2 digits
//     /dca/1        1 digit   <-- NOT zero-padded
//     /headamp/000  3 digits
// These builders exist so that mistake can only be made once.
//
// `suffix` is the trailing path with no leading slash, e.g. "grp/dca".
// An empty suffix yields the bare strip address.
QString chAddr(int channel, QLatin1String suffix = QLatin1String());
QString dcaAddr(int dca, QLatin1String suffix = QLatin1String());
QString headampAddr(int headamp, QLatin1String suffix = QLatin1String());

// ── Mute ─────────────────────────────────────────────────────────────
//
// There is no /ch/NN/mute address. Mute is expressed as /ch/NN/mix/on,
// INVERTED relative to the word "mute": 1 = ON = UNMUTED, 0 = MUTED.
// (The only "mute" under /ch is grp/mute, which is mute-GROUP membership.)
constexpr int  onValueForMuted(bool muted) { return muted ? 0 : 1; }
constexpr bool mutedFromOnValue(int on)    { return on == 0; }

// ── level: the fader / send dB curve ─────────────────────────────────
//
// Four linear segments approximating a log taper. Anchors: 0 dB <-> 0.75,
// +10 dB <-> 1.0, and f == 0.0 is -infinity (a hard off), NOT -90 dB.
//
// The bottom of the range is deliberately not perfectly invertible:
// dbToLevel(-inf) and dbToLevel(-90) both give 0.0, while levelToDb(0.0)
// gives -inf. That asymmetry is the console's, not ours.
float levelToDb(float f);
float dbToLevel(float db);

// The same curve is used at two different resolutions — getting these
// crossed puts values on the wrong grid and the console silently rounds.
//   Faders (/ch/NN/mix/fader, /dca/N/fader, masters): 1024 steps
//   Bus sends (/ch/NN/mix/NN/level, mlevel):           161 steps
float quantizeFader(float f);
float quantizeSend(float f);

// ── linf / logf ──────────────────────────────────────────────────────
//
//   linf: value = min + f*(max-min)
//   logf: value = min * (max/min)^f
//
// logf does NOT require min < max — see eqQ below, where min > max and the
// mapping is genuinely inverted. Both directions clamp f to [0,1].
float linfToValue(float f, float min, float max);
float valueToLinf(float value, float min, float max);
float logfToValue(float f, float min, float max);
float valueToLogf(float value, float min, float max);

// Snap to a grid of N discrete steps (the doc quotes N per parameter).
float quantizeSteps(float f, int steps);

// ── EQ ───────────────────────────────────────────────────────────────
//
// Input channels have 4 bands; buses/matrices/mains have 6 with a larger
// type enum. Do not assume a uniform EQ model across strip types.
inline constexpr float kEqFreqMin = 20.0f;
inline constexpr float kEqFreqMax = 20000.0f;
inline constexpr int   kEqFreqSteps = 201;

inline constexpr float kEqGainMin = -15.0f;
inline constexpr float kEqGainMax = 15.0f;

// NOTE min > max. This is not a typo and not a bug: EQ Q is logf[10.0, 0.3, 72],
// so f=0.0 is Q 10.0 (narrowest) and f=1.0 is Q 0.3 (widest). Reversing it
// yields plausible-but-wrong filters that no one notices until a show.
inline constexpr float kEqQMin   = 10.0f;
inline constexpr float kEqQMax   = 0.3f;
inline constexpr int   kEqQSteps = 72;

float eqFreqToFloat(float hz);
float floatToEqFreq(float f);
float eqGainToFloat(float db);
float floatToEqGain(float f);
float eqQToFloat(float q);
float floatToEqQ(float f);

} // namespace quewi::mix::x32
