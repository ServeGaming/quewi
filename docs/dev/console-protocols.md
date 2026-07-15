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

*(pending — research in flight)*
