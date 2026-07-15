# Console protocol reference

Working notes for `ConsoleLink` implementations (see `quewi-mix-spec.md`).

**Status: research, not verified on hardware.** Most of this comes from
third-party sources, not official specs. Every claim marked ⚠️ needs a real
console before we ship code that depends on it. Wrong addresses are worse than
missing ones — do not promote anything here to "known" without a test.

---

# Yamaha (RCP / SCP)

## Headline: the public parameter dumps are stale — trust them for CL/QL, not for scope

Initial research concluded **TF cannot do DCA assignment over the network**,
because the TF parameter table (118 entries, a literal `prminfo` dump from a
console) contains zero `Assign` addresses, while CL/QL, Rivage and DM7 all have
`InCh/DCA/Assign` in the identical dump format.

**That conclusion is wrong, and the way it's wrong matters.**

TheatreMix ships support for **TF1/TF3/TF5**, and DCA assignment is its entire
core feature. So DCA assignment over the network on TF demonstrably works. The
capability exists; the public dump doesn't show it.

**The likely explanation is firmware.** TheatreMix's console list specifies
minimum firmware:

| Family | TheatreMix minimum | Tested |
|---|---|---|
| Yamaha TF1/TF3/TF5 | **4.00** | 4.56 |
| Yamaha QL1/QL5, CL1/CL3/CL5 | **5.10** | 5.91 |
| Yamaha DM7 | 1.52 | 1.75 |
| Behringer X32 / Midas M32 | 2.12 | 4.13 |
| Behringer WING | 2.0 | 3.1 |

Those floors are not arbitrary. The most economical reading is that the
parameters TheatreMix needs **appeared in those firmware versions**, and the
Companion dumps were captured on older firmware.

### What this means for us

1. **Do not treat the Companion dumps as a capability ceiling.** They are a
   *floor* — everything in them is real, but absence proves nothing.
2. **Re-dump `prminfo` on current firmware** before scoping the Yamaha half.
   This is the single highest-value hour of hardware time in the project.
3. The same explanation probably resolves the EQ contradiction below.
4. The lead in *Documentation status* (the console self-describes its parameter
   table) stops being a nice-to-have and becomes **the** way to do this: query
   the console for what it actually supports on the firmware in front of us,
   rather than hardcoding a table that was already stale when we found it.

⚠️ Everything in the CL/QL section below is positively confirmed by the dump and
is safe to build on. What is *not* safe is any conclusion of the form "X is
impossible because it isn't in the dump."

## Transport (all Yamaha families)

Same protocol across CL/QL, TF, DM3/DM7, Rivage. Differences are in the
*parameter table*, not the wire format.

- **TCP**, port **49280**. Not UDP.
- Plain ASCII, line-oriented, **LF** (`\n`) terminated.
- Space-separated tokens; quoted strings may contain spaces.
- **No handshake, no authentication.** Open the socket and send.

```
get <Address> <X> <Y>
set <Address> <X> <Y> <Val>
```

Reply fields: `Status Action Address X Y Val TxtVal`
`Status` ∈ `OK` | `OKm` | `NOTIFY` | `ERROR`. (⚠️ `OKm` semantics unknown.)

`X` = channel index, `Y` = secondary index (DCA/mix/matrix). **Both 0-based.**
Scenes are **1-based**.

### Value encoding — two different scales, easy to get wrong

| Type | Encoding |
|---|---|
| dB levels | integer, **dB × 100**. `1000` = +10.00 dB |
| −∞ | `-32768` |
| **Dynamics threshold** | **dB × 10**, not ×100. `-260` = −26.0 dB |
| HA gain | ×100, `-600`…`6600` = −6.00…+66.00 dB |
| Pan / balance | integer **−63…+63**, unscaled |
| Booleans | `0` / `1` |
| Colours | **quoted string name**, not an index |

## CL / QL

Topology (from a **CL5** dump — smaller models have fewer channels;
**do not hardcode 72**): 72 InCh · 16 Mix · 8 Mtrx · **16 DCA** · 8 MuteMaster.

**CL/QL have 16 DCAs, not 8.**

### DCA assignment ✅

```
set MIXER:Current/InCh/DCA/Assign <ch 0-71> <dca 0-15> <0|1>
```

Per-(channel, DCA) **boolean** — not a bitmask, not a membership list. One
message per pair. A full 72×16 state sync is **1152 `get`s** — rate-limit
(~5 ms spacing) and cache.

⚠️ Use `StInCh/DCA/Assign`, not `StIn/DCA/Assign` (the latter is read-only and
appears to be a legacy duplicate).

### DCA control

```
set MIXER:Current/DCA/Fader/Level <dca> 0 -1000    # -10.00 dB
set MIXER:Current/DCA/Fader/On    <dca> 0 1
set MIXER:Current/DCA/Label/Name  <dca> 0 "Cast"   # max 8 chars
set MIXER:Current/DCA/Label/Color <dca> 0 "Yellow"
```

Colours: `Blue, Orange, Yellow, Purple, Cyan, Magenta, Red, Green, Off`.

### Mute polarity ⚠️

```
set MIXER:Current/InCh/Fader/On <ch> 0 0    # 0 MUTES the channel
```

`On` is **channel-on, not mute**. `1` = unmuted (default). Inverted from the
obvious reading.

⚠️ `MuteMaster/On` defaults to `0` while `Fader/On` defaults to `1`, implying
`MuteMaster/On = 1` means "mute group **active**" — opposite polarity.
**Undocumented. Verify on hardware.**

### What CL/QL does NOT expose

**No EQ. No HPF. No channel delay. No full dynamics** (threshold only).
Available: `InCh/Port/HA/Gain`, `InCh/Dyna1/Threshold` (gate),
`InCh/Dyna2/Threshold` (comp), name/colour/icon, pan, mix/matrix sends.

⚠️ **This contradicts TheatreMix, which advertises channel profiles with EQ,
dynamics, gain and HPF on Yamaha.** Given the firmware finding above, the most
likely explanation is that **the dump is from firmware older than TheatreMix's
5.10 floor** and current CL/QL firmware does expose EQ. Do not conclude phase 3
is impossible on Yamaha — conclude that we need a fresh dump.

Other possibilities, in rough order of likelihood: TheatreMix uses console
snippet recall for profiles rather than direct parameter writes; or it has NDA
protocol access we don't. **Unresolved until we dump a current console.**

### Metering ⚠️ — inverted from expectation

**CL/QL exposes only Mix and Matrix output meters (PostOn). No input
metering.** TF, by contrast, has full input metering with three pickoffs.

```
mtrstart MIXER:Current/Mix/PostOn 80      # interval ms, 40-1000
```
Subscription **expires — re-arm every ~10 s**. Data is hex-encoded, mapped via
a lookup table. ⚠️ `mtrinfo` declares 0…127 but the table spans 0…255 with a
+126 offset in one path — **scaling unresolved, verify on hardware.**

**Consequence (⚠️ same caveat as EQ):** silent/clip mic detection (spec phase 5)
needs *input* meters. If current CL/QL firmware really lacks them, that feature
is TF-only and the UI must degrade honestly rather than show a dead meter.
TheatreMix advertises channel monitoring across its console range, which is
again evidence the dump is stale rather than the capability absent. Verify
before scoping.

### Scene recall

```
ssrecall_ex MIXER:Lib/Scene 5        # 1-based, max 300
sscurrent_ex MIXER:Lib/Scene         # reply carries modified|unmodified
```
TF instead uses banks: `ssrecall_ex scene_a 5` / `scene_b`, max 100.
⚠️ TF quirk: no way to tell which bank is active except by querying the other
and reading the error.

## Change notification ✅ — the live-edit enabler

The console **pushes unsolicited `NOTIFY set …`** when a parameter changes on
the surface or via another client. No subscription needed (unlike metering).

- Your own `set` → `OK set …` (echo). Someone else's → `NOTIFY set …`.
  **The Status field is what makes loop-free live capture possible.**
- Corroborated by the official MTX/MRX spec §4.6.
- ⚠️ Changes are "sampled" — rapid fader moves are coalesced, not
  sample-accurate. Expect decimated streams.
- **Poll after a scene recall** — a recall changes a huge swathe of state and
  the console does not enumerate every changed parameter.

## Discovery ❌

None documented. **Plan for a user-typed static IP.** StageMix auto-detects,
but the mechanism is proprietary and unverified — don't build on it without a
packet capture.

## Offline editors ❌ — hardware required

CL/QL Editor and TF Editor are RCP **clients, not servers**. They don't listen
on 49280. *"This module only works to connected hardware. It does not work with
the Editor."*

**Mitigation: build a mock console.** The `prminfo` dumps give exact addresses,
ranges and types per model — enough for a faithful TCP simulator on 49280 that
answers `get`/`set` and emits `NOTIFY`. This is the only hardware-free path and
it's tractable.

## Connection limits ⚠️

TF: 3 editors/StageMix total. CL/QL: one StageMix + one Editor simultaneously.
⚠️ **Whether an RCP client consumes a StageMix/Editor slot is undocumented.**
Evidence suggests it coexists (Companion + TF Editor run together), but verify
— it matters if a venue runs StageMix alongside quewi.

## Other families

Per the stale-dump finding, ❌ here means **"absent from the dump we have"**, not
"impossible". Only the ✅ entries are load-bearing.

| | DCAs | DCA assign | EQ/HPF |
|---|---|---|---|
| Rivage PM | 24 | ✅ 288×24 | ✅ extensive |
| DM7 | 24 | ✅ 120×24 | ✅ |
| DM3 | — | ❌ none in dump ⚠️ | partial |
| CL/QL | 16 | ✅ 72×16 | ❌ in dump ⚠️ |
| TF | 8 | ❌ in dump ⚠️ (TheatreMix ships TF support — it exists) | ❌ in dump ⚠️ |

DM3 and Rivage have **official public OSC specs** — documented, no NDA:
- [RIVAGE PM OSC v1.0.2](https://usa.yamaha.com/files/download/other_assets/5/1407565/RIVAGE_PM_osc_specs_v102_en.pdf)
- [DM3 OSC v1.0.0](https://fr.yamaha.com/files/download/other_assets/2/2063222/DM3_osc_specs_v100_en.pdf)

## Documentation status

**There is no public official spec for CL/QL/TF remote control.** The protocol
is under NDA. Primary evidence for everything above is
[bitfocus/companion-module-yamaha-rcp](https://github.com/bitfocus/companion-module-yamaha-rcp),
whose parameter files are literal `prminfo` responses captured from real
consoles.

Only official public doc of the wire format:
[MTX/MRX/XMV RCP Spec v3.1.0](https://data.yamaha.com/files/download/other_assets/1/1144121/mtx_mrx_xmv_ex_remote_control_protocol_spec_v310_en.pdf)
— same transport and envelope, different addressing.

**Lead worth an hour on hardware:** the dumps are console `prminfo` responses,
so **the console can self-describe its parameter table**. The query syntax isn't
documented anywhere. Finding it would let us enumerate per-model capability at
runtime instead of hardcoding channel counts — the clean solution to the
"don't hardcode 72" problem.

**Trap:** the BrenekH docs' example `MIXER:Current/InCh/Fader/Name` is wrong and
contradicts its own command list. The real address is `InCh/Label/Name`.

---

# Behringer X32 / Midas M32

Unlike Yamaha, this protocol is **well documented and independently verified**.
Primary source: Patrick-Gilles Maillot, *Unofficial X32/M32 OSC Remote Protocol*
v4.06 rev 09 (181pp, console FW 4.0+) — cited as **[OSC-DOC]**. Corroborated
against [pmaillot's emulator source](https://github.com/pmaillot/X32-Behringer),
the [Bitfocus Companion X32 module](https://github.com/bitfocus/companion-module-behringer-x32)
(hardware-tested), and [Games Done Quick's real production scene files](https://github.com/GamesDoneQuick/digital-mixer-configs).

Claims below are triple-sourced unless marked ⚠️.

## Transport

- **UDP, port 10023.** (XAir series is 10024 and a *different* protocol — don't
  scan into it by accident.)
- OSC 1.0, big-endian, 4-byte aligned. Types used: `i`, `f`, `s`, `b` only.
  **No bundles, no timetags, no `T`/`F`.**
- Console replies **to the source IP:port of the request** — no fixed return
  port. (This is why we can't reuse `OscEngine`; see the spec.)
- Get = send the address with **no arguments**; the console echoes the value.
  Set = same address with an argument.

⚠️ **Meter/subscription blob payloads are LITTLE-endian inside.** The OSC blob
*size* field is standard big-endian; everything within the blob is native x86
order. Biggest trap in the protocol.

## DCA assignment ✅ — the headline

```
/ch/[01…32]/grp/dca   ,i   8-bit bitmask [0,255]
/ch/[01…32]/grp/mute  ,i   6-bit bitmask [0,63]
```

**DCA n ↔ bit (n−1) ↔ value 1 << (n−1). DCA1 = bit 0 = value 1.**

[OSC-DOC] says only "8 bits bitmap" and **never states which bit is DCA1.** It
was verified three independent ways rather than guessed:

1. **Real production scene data.** GDQ's `.scn` declares `/dca/1/config "Break
   Music"`, `/dca/2/config "Run"`, `/dca/3/config "Interview"`. Correlating 22
   channels' names against their bitmaps: channels named `Runner 1…4` carry
   `%00000010` → MSB-first reads DCA2 "Run" ✓; LSB-first reads DCA7 ✗.
   `InterviewLav` carries `%00000100` → DCA3 "Interview" ✓. MSB-first is
   perfect across all 22; LSB-first is nonsense.
2. **Emulator source** renders `%int` MSB-first down to bit 0 — so `%00000010`
   is literally `int 2`.
3. **Consistency**: `%00000001` = DCA1, `%10000000` = DCA8.

Same `grp/dca` pair exists on `/auxin/`, `/fxrtn/`, `/bus/`, `/mtx/`,
`/main/st`, `/main/m` — that's how you DCA-assign buses.

### ⚠️ It's replace-not-toggle

There is **no per-DCA-membership address**. To add DCA3 you must
read-modify-write: `new = old | 4`. Cache state and always send the full mask.
This is the opposite of Yamaha's per-pair boolean and the single biggest shape
difference between the two links.

## DCA control

⚠️ **DCA addresses are single-digit `/dca/1`…`/dca/8`** — *not* zero-padded like
`/ch/01`. Common bug.

```
/dca/[1…8]/on            enum {OFF,ON}   1 = UNMUTED
/dca/[1…8]/fader         level [0.0…1.0], 1024 steps
/dca/[1…8]/config/name   string, 12 chars max
/dca/[1…8]/config/color  enum [0…15]
```

⚠️ **[OSC-DOC] p.131 lists `/dca/N/mix/on` and `/dca/N/mix/fader`. That is a
documentation error** (copy-paste bleed from the channel column) — it
contradicts p.39 and appears in neither the emulator nor Companion. Use
`/dca/N/on` and `/dca/N/fader`.

Colours: `0 OFF, 1 RD, 2 GN, 3 YE, 4 BL, 5 MG, 6 CY, 7 WH`, plus 8–15 as
inverted-scribble variants. Accepts `,i 2` or `,s "GN"`.

## Mute ⚠️ — same inversion as Yamaha

```
/ch/[01…32]/mix/on   ,i   0 = MUTED, 1 = UNMUTED
```

**There is no `/ch/NN/mute` address.** The only `mute` under `/ch` is
`grp/mute` (mute-*group* membership). Confirmed by the doc's own captured
console trace, narrated as channels "being muted… then successively unmuted",
where unmuting yields `1`.

Both consoles use on-not-mute semantics — at least the inversion is *consistent*
across the two links.

## Value encodings

Every float on the wire is `[0.0…1.0]`. The console recognises only a finite
grid of discrete values; out-of-grid floats are **rounded, not rejected**.

```
linf:  value = min + f*(max-min)
logf:  value = min * (max/min)^f
grid:  f = round(f*(N-1))/(N-1)
```

### The `level` dB curve (faders and sends)

Four linear segments. Verbatim from [OSC-DOC] p.132 and **byte-identical** in
Companion's independently-written `util.ts`:

```c
if      (f >= 0.5)    d = f*40.  - 30.;   // max +10 dB
else if (f >= 0.25)   d = f*80.  - 50.;
else if (f >= 0.0625) d = f*160. - 70.;
else if (f >= 0.0)    d = f*480. - 90.;   // f == 0.0 is -inf, NOT -90
```

Anchors: **0 dB ↔ 0.75**, **+10 dB ↔ 1.0**, **f=0.0 ↔ −∞** (hard off).

⚠️ **Two different step counts share this curve.** Faders and DCA faders:
**1024** steps (`f = int(f*1023.5)/1023.0`). Bus sends / mlevel: **161** steps.
⚠️ The 161-step rounding (`round(f*160)/160`) is extrapolated, not stated.

### ⚠️ EQ Q is INVERTED

`logf [10.000, 0.3, 72]` — **min > max**. f=0.0 → Q 10.0 (narrowest); f=1.0 →
Q 0.3 (widest). Getting this backwards silently produces plausible-but-wrong
filters. Input channels have **4 EQ bands**; buses/matrices/mains have **6**
with a larger type enum — do not assume a uniform EQ model across strip types.

### Channel processing (for phase 4 profiles)

```
/ch/NN/preamp/trim     linf [-18, 18, 0.25] dB   (digital sources only)
/ch/NN/preamp/hpon     enum {OFF,ON}
/ch/NN/preamp/hpf      logf [20, 400, 101] Hz
/ch/NN/preamp/hpslope  enum {12, 18, 24} dB/oct
/ch/NN/eq/[1…4]/{type,f,g,q}
/ch/NN/dyn/{on,mode,thr,ratio,knee,mgain,attack,hold,release,…}
/ch/NN/gate/{on,mode,thr,range,attack,hold,release,…}
/ch/NN/mix/pan         linf [-100, 100, 2.0]     (only 101 positions)
/ch/NN/delay/{on,time} linf [0.3, 500.0, 0.1] ms
```

⚠️ **Analogue head-amp gain is NOT on the channel.** It lives on a global map:
`/headamp/[000…127]/gain` — **3-digit zero-padded** — with `/-ha/[00…39]/index`
(read-only) telling you which headamp feeds which channel. Three different
zero-padding widths in one protocol (`/dca/1`, `/ch/01`, `/headamp/000`).

## Metering ✅

```
/meters ,si  <meter_id> [time_factor]
```
- Base rate **50 ms**; `time_factor` ∈ [1,99] → interval = 50 ms × factor. Max
  ~20 Hz.
- **Timeout 10 s — must be renewed.**
- `/meters/1` → **96 floats: 32 input channels + 32 gate GR + 32 dyn GR.**
  That's input metering in one blob — silent/clip detection (spec phase 6) is
  **confirmed possible on X32.**
- `/meters/5` with `grp_id=0` → the 8 DCA meters. No dedicated DCA-only ID.
- Values are **linear**, digital 0 = full scale. ⚠️ **Internal headroom allows
  values up to 8.0 (+18 dBFS)** — do not assume ≤1.0 when detecting clipping.

Blob parse: trust the OSC blob size (BE), **skip the first 4 payload bytes,
read the rest as LE floats.** ⚠️ The doc is internally inconsistent about
whether that leading int is a count or a byte-length; deriving the count from
the blob size sidesteps it.

## Subscription — `/xremote`

```
/xremote    (no arguments)
```
- Registers for **all** parameter changes made elsewhere. Keyed on (IP, source
  port). **Timeout 10 s.** Companion re-sends every **1.5 s** — a 6.6× margin
  over lossy Wi-Fi, battle-tested.

### ⚠️⚠️ The console does NOT echo your own changes back to you

> "the X32 console echoes the value of a console parameter in response to a set
> command **from another client**" — [OSC-DOC] p.10

A set you originate produces **no confirmation on your `/xremote` socket.** Over
UDP, with no delivery guarantee, that means a silently-dropped mute is
undetectable.

**The fix (Companion does exactly this in production):** register `/xremote`
from socket A; send all sets from socket B on a different source port. The
console treats B as "another client" and relays the change to A — closed-loop
confirmation. **Do this. Never fire-and-forget a mute.**

## Discovery ✅ — unlike Yamaha, this works

```
Send /xinfo (no args) → UDP broadcast 255.255.255.255:10023
Listen on your ephemeral source port
```
Reply: `/xinfo ,ssss <ip> <name> <model> <fw>`.
`/info ,ssss <server_ver> <server_name> <console_model> <console_ver>` — arg[2]
identifies the variant: `X32`, `X32C`, `X32P`, `X32RACK`, `X32CORE`, `M32`,
`M32C`, `M32R`. Companion re-broadcasts every 30 s, prunes at 60 s. Requires
same subnet — broadcast doesn't cross routers.

## Model variants — the protocol is identical

**Every X32/M32 variant exposes the same OSC surface**: 32 input channels, 8 aux
in, 8 FX returns, 16 mix buses, 6 matrices, Main L/R, Main M/C, **8 DCAs**. The
differences are physical (faders, local preamps, screen), not protocol.
Companion hard-codes 32 channels and 8 DCAs with no model branching.

M32 vs X32: same addresses, same port, different `/info` string, different
preamp voicing. No documented protocol differences.

## Scene recall

```
/-action/goscene   ,i [0…99]
/-action/gosnippet ,i [0…99]
/-action/gocue     ,i [0…99]
```
Index-based (slot 0–99), **not name-based.**

## ⚠️⚠️⚠️ Three deployment landmines

These are the findings that change the product, not just the code.

### 1. Scene recall silently clobbers DCA assignments

Scene Safe bitmap for input channels (`/-show/showfile/show/inputs`, 8-bit):

```
bit 0: Preamp    bit 1: Config   bit 2: EQ      bit 3: Gate & Comp
bit 4: Insert    bit 5: Groups (DCA assign, Mute group assign)  ← THIS
bit 6: Faders, Pan               bit 7: Mute
```

If the operator — **or our own `/-action/goscene`** — recalls a scene without
**bit 5** safed, every assignment we made is silently reverted. Mid-show. No
error. **The interaction model has to address this explicitly**, not discover it
at a tech.

### 2. Channel links cause mystery double-moves

`/config/chlink/1-2` … `/31-32`. If a pair is linked, writing one channel's
fader or mute **moves its partner too**. `/config/linkcfg/fdrmute` controls
whether mute/fader are in the link. **Read `/config/chlink/*` at startup** or
we'll produce moves the operator didn't ask for and can't explain.

### 3. Only FOUR `/xremote` clients, total

> "Triggers X32 to send all parameter changes to maximum four active clients."

X32-Edit, a tablet on the desk, any Companion instance, and quewi all compete
for four slots. In a real theatre rig those slots are scarce. **This needs to be
visible in the UI** — silently failing to register is the worst outcome.

Also read before assuming behaviour: `/-prefs/dcamute`, `/-prefs/hardmute`,
`/-prefs/invertmutes` (`{NORM, INV}`) all change how DCA mutes behave.

## Rate limiting ⚠️

**No documented limit exists anywhere.** Measure; don't assume. What's known:
- Meters cap at 50 ms internally, "variable according to console's ability."
- The doc's only throughput warning is about the **return** path: heavy polling
  overruns UDP buffers and "no errors will be reported in UDP for loss of data."
  54 Mbit Wi-Fi is called inadequate for shows >20 cues; **100 Mbit wired
  recommended.**
- Companion (widely deployed): request queue concurrency **20**, **500 ms**
  per-request timeout, fader fades default **10 fps** (configurable 5–60).

Recommendation: 10–30 fps per moving fader, batched; two-socket confirmation;
verify final state by reading back rather than trusting the send.

## Development without hardware ✅ — solved

[pmaillot's X32 emulator](https://github.com/pmaillot/X32-Behringer) (`X32.c`,
plain C, builds on Windows/Linux/macOS, v0.88 tracking FW 4.06, updated 2024):
32 channels, 16 bus, 8 DCA, **keeps up to 4 `/xremote` clients updated** —
mirrors the real client limit. Its per-section command tables (`X32Dca.h`,
`X32Channel.h`, …) double as a machine-readable address/type list.

```
X32 -i 127.0.0.1        # port hard-coded to 10023
```

Point quewi at `127.0.0.1:10023`. This is the development loop.

⚠️ **Fidelity is imperfect — a dev harness, not a conformance oracle.** Known
example: [issue #18](https://github.com/pmaillot/X32-Behringer/issues/18) — the
emulator stores `solosw` correctly but doesn't relay it to X32-Edit. Validate
against real iron before shipping.

⚠️ The emulator `bind()`s one unicast IP rather than `0.0.0.0`, so X32-Edit's
broadcast auto-discovery probably won't find it — enter the IP manually. (This
is reasoned from the source plus socket semantics, **not tested.**)

## Uncertainty register

| Item | Status |
|---|---|
| `/-stat/dcaspill` semantics | Address exists; the PDF table is mangled by column bleed. Verify on hardware. |
| Blob leading int: count vs byte-length | Doc is self-inconsistent; both readings backed by its own hex dumps. Sidestep via blob size. |
| Emulator + X32-Edit loopback discovery | Reasoned, not tested. |
| `/auxin/../preamp/trim` range | Doc says ±18, python-x32 says ±12. `/ch` ±18 is solid; auxin isn't. |
| 161-step rounding | Extrapolated from the 1024-step formula; consistent with the doc's table but not stated. |
| Max message rate | **No documented limit.** Companion's numbers are empirical. |

**Highest confidence** (doc + emulator + hardware-tested implementation, and for
DCA also real production scene data): UDP/10023, `mix/on` polarity, **DCA1 = bit
0 = value 1**, `/dca/N/{on,fader,config/*}`, the `level` curve, `/xremote` 10 s /
4 clients, `/xinfo` broadcast discovery, no-self-echo.
