#pragma once

#include <QString>
#include <QStringList>
#include <optional>

// Value encodings and line framing for the Yamaha DM7 RCP protocol
// (TCP 49280, ASCII, LF-terminated). Pure functions, no I/O.
//
// Sourcing and the disputed values: docs/dev/console-protocols.md.
//
// A standing warning that shaped this file: the community parameter tables for
// Yamaha consoles are CURATED snapshots that under-report — they show no
// DCA-assign on TF even though TheatreMix ships TF support. Absence in a table
// proves nothing. Where Yamaha's own official spec disagrees with the
// community table, the official spec wins here, and where all sources
// disagree the function refuses to guess.
namespace quewi::mix::dm7 {

// ── Topology ─────────────────────────────────────────────────────────
// DM7 and DM7 Compact differ ONLY in input count. Everything else — DCAs,
// mute groups, buses — is identical, so the core logic is model-independent.
// Do not hardcode the channel count: probe it.
inline constexpr int kDcaCount        = 24;
inline constexpr int kMuteGroupCount  = 12;
inline constexpr int kChannelsDm7     = 120;
inline constexpr int kChannelsCompact = 72;

// ── Line framing ─────────────────────────────────────────────────────
//
// Space-separated tokens; quoted strings may contain spaces. Backslash is the
// escape character: \\ is a literal backslash, \" a literal quote.
QStringList tokenize(const QString &line);

// Wrap in quotes and escape. Use for every string argument.
QString quote(const QString &s);

// A parsed reply line. Field order is fixed by the protocol:
//   Status Action Address X Y Val [TxtVal]
struct Reply {
    enum class Status { Ok, OkModified, Notify, Error, Unknown };

    Status  status = Status::Unknown;
    QString action;      // "set", "get", "devinfo", ...
    QString address;     // "MIXER:Current/InCh/DCA/Assign"
    int     x = -1;      // primary index (channel), 0-based on the wire
    int     y = -1;      // secondary index (DCA/mix), 0-based
    QString value;       // raw, unquoted
    QString textValue;   // trailing TxtVal, when present

    // The single most important distinction in the protocol: an `Ok` is the
    // console echoing OUR write; a `Notify` is somebody touching the surface.
    // Treating our own echo as a surface change is how a feedback loop starts.
    bool isSurfaceChange() const { return status == Status::Notify; }
};

std::optional<Reply> parseReply(const QString &line);

// ── Addresses ────────────────────────────────────────────────────────
//
// The interface is 1-based (what's printed on the desk); the wire is 0-based.
// These do the conversion in one place so it can't be got wrong twice.
QString setCommand(const QString &address, int x0, int y0, const QString &rawValue);
QString getCommand(const QString &address, int x0, int y0);

inline constexpr auto kDcaAssign = "MIXER:Current/InCh/DCA/Assign";
inline constexpr auto kChannelOn = "MIXER:Current/InCh/Fader/On";
inline constexpr auto kDcaName   = "MIXER:Current/DCA/Label/Name";
inline constexpr auto kDcaColor  = "MIXER:Current/DCA/Label/Color";
inline constexpr auto kDcaLevel  = "MIXER:Current/DCA/Fader/Level";
inline constexpr auto kMuteGrpOn = "MIXER:Current/MuteGrpCtrl/On";  // NOT MuteMaster
inline constexpr auto kSplitOn       = "MIXER:Setup/Unit/Split/On";
inline constexpr auto kSplitDcaStart = "MIXER:Setup/Unit/Split/DCA/StartCh";
inline constexpr auto kSplitDcaNum   = "MIXER:Setup/Unit/Split/DCA/Num";

// ── Mute ─────────────────────────────────────────────────────────────
//
// `Fader/On` is channel-on, not mute: 1 = unmuted (the default), 0 = muted.
// Same polarity as the X32, at least.
//
// NOTE the mute-GROUP address is believed to be the opposite (1 = mute
// engaged) — its default is 0 where Fader/On's is 1 — but that is UNVERIFIED
// and the official spec's "0: OFF, 1: ON" is ambiguous about what ON means.
// No helper for it here on purpose: getting it backwards mutes the cast
// mid-show, so it doesn't get a convenient wrapper until hardware says.
constexpr int  onValueForMuted(bool muted) { return muted ? 0 : 1; }
constexpr bool mutedFromOnValue(int on)    { return on == 0; }

// ── dB ───────────────────────────────────────────────────────────────
//
// Levels are integer dB x100. -32768 is -infinity; +1000 is +10.00 dB, the
// ceiling. -13800 (-138.0 dB) is the lowest discrete value, confirmed by
// Yamaha's own value table.
//
// (Dynamics thresholds use x10, not x100 — a separate trap. No helper here
// because DM7 dynamics aren't confirmed to be addressable at all yet.)
inline constexpr int kLevelNegInf  = -32768;
inline constexpr int kLevelMinReal = -13800;
inline constexpr int kLevelMax     =   1000;

int   dbToRaw(float db);
float rawToDb(int raw);          // returns -inf for kLevelNegInf

// ── Pan ──────────────────────────────────────────────────────────────
//
// Pan is NOT continuous. Yamaha's Table 2 enumerates 27 legal values: steps
// of 5 from -60 to +60, plus +/-63 at the ends. Sending -62 is rejected or
// snapped by the console, so snap it ourselves and stay predictable.
int snapPan(int pan);
bool isLegalPan(int pan);

} // namespace quewi::mix::dm7
