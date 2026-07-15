# Console protocol reference

Working notes for `ConsoleLink` implementations (see `quewi-mix-spec.md`).

**Status: research, not verified on hardware.** Most of this comes from
third-party sources, not official specs. Every claim marked ⚠️ needs a real
console before we ship code that depends on it. Wrong addresses are worse than
missing ones — do not promote anything here to "known" without a test.

---

# Yamaha — DM7 (our target)

## Headline: the published dumps are wrong, and Yamaha publishes better ones

Initial research concluded **TF cannot do DCA assignment over the network**,
because the published TF parameter table contains zero `Assign` addresses while
CL/QL, Rivage and DM7 all have `InCh/DCA/Assign` in the identical format.

**That conclusion was wrong, and how it was wrong set the method for everything
below.** TheatreMix ships TF support at firmware ≥4.00, and DCA assign is its
entire product — so it demonstrably works. The capability exists; the dump
doesn't show it. TheatreMix's firmware floors (TF ≥4.00, CL/QL ≥5.10, DM7 ≥1.52,
X32 ≥2.12) are very likely the versions where those parameters appeared.

Following that thread turned up three things that reframe the Yamaha half:

### 1. The dumps are *curated*, not merely stale

The DM7 file **skips index 101** (`Mtrx/PEQ/Band/Freq`, which the official spec
confirms exists) and **omits indices 122–155 entirely** — 34 consecutive slots.
A genuine full enumeration has no holes. File dates confirm the gradient: TF's
table last changed **2024-01-16**, CL/QL's **2023-12-11**, DM7's **2026-04-26**.

**Rule applied throughout: presence in a dump is evidence *for*; absence is *no
information*.**

### 2. Yamaha publishes an official, current, public parameter spec for DM7

**[DM7 Series OSC Specifications V1.1.0](https://data.yamaha.com/files/download/other_assets/5/2234295/DM7_osc_specs_V110_en.pdf)**
(July 2025) documents **the same `MIXER:Current/…` parameter IDs** that RCP uses,
with Yamaha's own min/max/scaling/units and value tables. No NDA.

This is the primary reference now. It **proves the community dump wrong in at
least eight places** (see the ledger below).

### 3. The self-description query is officially documented

**[DME7 Remote Control Protocol Spec V1.1.0](https://usa.yamaha.com/files/download/other_assets/4/2230684/DME7-remote-V110_en.pdf)**
documents `prmnum` / `prminfo` / `mtrnum` / `mtrinfo` — **the console
enumerating its own parameter table.** The MTX/MRX spec has zero occurrences of
`prminfo`; this one has eleven, with a response format that matches the dump
lines field-for-field. That's how those files were made.

If a DM7 *console* accepts the query (documented for DME7, ⚠️ unproven on
consoles), we can enumerate the live desk at connect time and **retire this
entire class of error permanently.** That's what `tools/dm7_probe.py` Test B is
for, and it's the highest-value command in the project.

## ⚠️ DM7 OSC is a trap — use RCP

DM7 has an OSC interface on **UDP 49900**. Do not use it: it is **write-only**.
194 `set` rows, zero `get` rows, `yosc:req` with no `yosc:res`, no notification,
no subscription. **It cannot read state or capture live edits.**

**RCP over TCP 49280 is mandatory for our tool.** The OSC spec's value is purely
as documentation of the parameter table.

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

## DM7 topology

**[CONFIRMED-OFFICIAL]**: **120 InCh** (DM7) / **72** (Compact) · 48 Mix ·
12 Matrix · 2 Stereo (+Mono) · **24 DCA** · **12 Mute Groups**.

**The only protocol-relevant DM7 vs DM7 Compact difference is input count
(120 vs 72).** DCA count, mute groups and all bus counts are identical, so the
core DCA logic is model-independent.

⚠️ **Don't hardcode 120.** The Companion module has one DM7 profile hardcoded to
120 and doesn't distinguish Compact (its changelog shows it was *previously* 72
— they flip-flopped). On a Compact, channels 73–120 don't exist. Read the count
from `prminfo`, or `devinfo productname`, or probe
`get MIXER:Current/InCh/Fader/Level 72 0` and expect an error on a Compact.

### ⚠️ Split mode — read this before touching a DCA index

```
MIXER:Setup/Unit/Split/On           r    0/1
MIXER:Setup/Unit/Split/DCA/StartCh  r    0..24
MIXER:Setup/Unit/Split/DCA/Num      r    0..24
MIXER:Setup/Unit/Split/InCh/StartCh r    0..120
MIXER:Setup/Unit/Split/Mute/StartCh r    0..12
```

**DM7 can run as two independent mixers, partitioning channels, DCAs and mute
groups between two units.** All read-only. **Read `Split/On` at startup and
honour `DCA/StartCh` + `DCA/Num`**, or DCA indices are wrong on a split console.
No equivalent exists on CL/QL or TF. ⚠️ [CONFIRMED-DUMP], not in the OSC spec —
verify.

## DCA assignment ✅ — confirmed by official spec

```
set MIXER:Current/InCh/DCA/Assign <ch 0-119> <dca 0-23> <0|1>
get MIXER:Current/InCh/DCA/Assign 0 0
```

Official row: *"Input Channel DCA Group Assign — X: 1-Input Channel Num —
Y: 1-DCA Group Num — 0: OFF, 1: ON"*.

**[CONFIRMED-OFFICIAL] 120 × 24, per-(channel, DCA) boolean.** Not a bitmask,
not a membership list — the opposite shape from X32.

**A full sync is 120 × 24 = 2880 messages.** At Companion's 5 ms spacing that's
~14 seconds. Cache aggressively and live on `NOTIFY` thereafter.

Also official: `Mix/DCA/Assign` (48×24), `Mtrx/DCA/Assign` (12×24),
`St/DCA/Assign` — DM7 supports **Output DCA**.

⚠️ Dump error: `St/DCA/Assign` shows X=12 but `St/Fader/Level` shows X=4.
Official says X = Stereo channel count. The dump's 12 is copy-paste from Mtrx.

## DCA control

| Address | X | Range | Scale |
|---|---|---|---|
| `MIXER:Current/DCA/Fader/Level` | 24 | −32768…1000 | **100** |
| `MIXER:Current/DCA/Fader/On` | 24 | 0/1, default **1** | 1 |
| `MIXER:Current/DCA/Label/Name` | 24 | string | — |
| `MIXER:Current/DCA/Label/Color` | 24 | string | — |

**[CONFIRMED-OFFICIAL]** fader table: `-32768` = **−∞**, `-13800` = −138.0 dB
(the real minimum discrete value), `1000` = **+10.00 dB**. Scale 100.

## Mute polarity ⚠️

```
set MIXER:Current/InCh/Fader/On <ch> 0 0     # 0 MUTES
```

**[CONFIRMED-OFFICIAL]** "0: OFF, 1: ON", default `1`. `On` is channel-on, not
mute — same polarity as X32, CL/QL and TF.

**DM7 uses `MuteGrpCtrl`, not CL/QL's and TF's `MuteMaster`.** 12 groups.

⚠️ **[DISPUTED] mute-group polarity.** `MuteGrpCtrl/On` defaults to `0` while
`Fader/On` defaults to `1`. Official says only "0: OFF, 1: ON" for both, which
is ambiguous about whether "ON" means *the mute is engaged*. Most likely `1` =
mute active, i.e. **opposite** to `Fader/On`. Companion's changelog ("Fix
missing MuteGrpCtrl/On Action for DM7") suggests this was historically fiddly.
**Test on hardware — getting it backwards mutes the cast mid-show.**

## Colours — DM7 has its own, and it's a superset

**[CONFIRMED-OFFICIAL]**, Table 3, **11 values**:

```
Blue  Green  Orange  Pink  Purple  Red  SkyBlue  Yellow  Cyan  Magenta  Off
```

DM7 has **both** CL/QL's `Cyan`/`Magenta` **and** TF's `SkyBlue`/`Pink`. Don't
inherit either family's list — Companion has no DM7 colour list at all.
Colours are **quoted strings** (the dump says `binary` — wrong) and there are
**11**, not the dump's 9.

## EQ ✅ — DM7 has it (unlike CL/QL and TF)

**Input channels: 4 bands. Mix/Matrix: 8 bands.**

| Address | X | Y | Range | Scale |
|---|---|---|---|---|
| `InCh/PEQ/On` | 120 | 1 | 0/1 | 1 |
| `InCh/PEQ/Type` | 120 | 1 | **string**: `PRECISE`,`AGGRESSIVE`,`SMOOTH`,`LEGACY` | — |
| `InCh/PEQ/Band/Bypass` | 120 | **4** | 0/1 | 1 |
| `InCh/PEQ/Band/Freq` | 120 | **4** | 200…200000 | **10** → 20 Hz–20 kHz |
| `InCh/PEQ/Band/Gain` | 120 | **4** | −1800…1800 | 🔴 **disputed** |
| `InCh/PEQ/Band/Q` | 120 | **4** | 100…16000 | **1000** → Q 0.1–16.0 |
| `InCh/HPF/{On,Freq,Slope}` | 120 | 1 | Freq 200…200000, Slope 6–24 dB/oct | Freq **10** |
| `InCh/LPF/{On,Freq,Slope}` | 120 | 1 | Slope 6–12 dB/oct | Freq **10** |

**This is why DM7 was the right target.** Channel profiles (spec phase 4) need
EQ/HPF, and CL/QL and TF appear to expose none.

> 🔴 **PEQ Band Gain scaling — three sources disagree. Must be tested.**
> - Companion dump: `InCh` scale **1**, `Mix` scale **100**
> - Official OSC V1.1.0: scale **10** for all
> - Physics: range −1800…1800 with ±18.00 dB ⇒ scale **100**
>
> Yamaha's own V1.1.0 revision note says *"Corrected values for PEQ Band Gain"* —
> they knew it was wrong and may have half-fixed it. **Scale 100 is most
> plausible; verify before writing any EQ.** Wrong = gains off by 10×.

## HA gain — the dump is provably wrong

```
Companion dump:  -6 … 66,    scale 1     <-- WRONG
Official OSC:    -600 … 6600, scale 100  = -6.00 … +66.00 dB
```
CL/QL's dump agrees with the official DM7 values, corroborating.

⚠️ Official warning: *"it is not always the case that Min < Max numerically… the
Min direction may be numerically larger, like the case of HA Gain."*

## Pan ⚠️ — not continuous

```
MIXER:Current/InCh/ToSt/Pan   ±63
```

Official **Table 2** enumerates only **27 legal values**:

```
-63 -60 -55 -50 -45 -40 -35 -30 -25 -20 -15 -10 -5  0  5 10 ... 55 60 63
```

Steps of 5, except ±63 at the ends. **Quantise before sending** — `-62` is
likely rejected or snapped.

## Dynamics and channel delay ⚠️ [UNPROVEN-ABSENCE]

**Absent from both the dump and the official OSC list.** Grepping the official
spec for `InCh/{Dyna,Gate,Comp,Delay,Insert}` gives zero hits.

But DM7 obviously *has* dynamics, and the spec exposes
`InputChLink/LinkParams/Dyna1`, `/Dyna2`, `/Delay`, `/Insert` (flags for whether
those are *linked*). So the features exist; they appear not to be remotely
addressable.

**Two independent sources agreeing — one official and current — is much stronger
than dump-alone. But per the rule above, still not proof.** CL/QL *does* expose
`Dyna1/Threshold`, so it'd be odd for DM7 to lack it.

**Hypothesis: dynamics live in the missing index block 122–155.** Probe
directly (`tools/dm7_probe.py` Test F). This decides how complete phase 4 can be.

## Metering ✅ — like TF, not CL/QL

```
mtrinfo 2000 "MIXER:Current/Meter/InCh" 120 3 ... "PreHPF|PreFader|PostOn"
mtrinfo 2100 "MIXER:Current/Meter/Mix"   48 3 ... "PreEQ|PreFader|PostOn"
```

**Full 120-channel input metering with three pickoffs → silent-channel and clip
detection will work.** (CL/QL only exposes Mix/Mtrx output meters.)

```
mtrstart MIXER:Current/InCh/PreFader 100     # interval ms, 40-1000
```
Note the `/Meter` segment is dropped and the pickoff appended.
**Subscription expires — re-arm every ~10 s.** Values are hex, mapped via a
lookup table (0 ≈ −190 dB … 254 ≈ +34 dB, 255 = sentinel).

⚠️ **[DISPUTED] scaling:** `mtrinfo` declares 0–127 but the table spans 0–255
and Companion applies a `+126` offset in one path. Its changelog — *"Fix
incorrect Meter values for DM7"* — confirms this was buggy specifically on DM7.
Verify empirically with a known tone.

## Scene handling ✅

```
scpmode sstype "text"          # REQUIRED FIRST on DM7
ssrecallt_ex scene_a "4.00"
sscurrentt_ex scene_a          → OK sscurrentt_ex scene_a "4.00" <modified|unmodified>
ssupdatet_ex scene_a "4.00"    # STORE — no confirmation, overwrites silently
```

- **`scpmode sstype "text"` must be sent first** or the `t`-variants misbehave.
- Scene numbers are **quoted `"x.xx"` strings**, "1.00"–"499.99", max 500.
  Sub-scene decimals (4.01, 4.02) are real.
- **Two banks, `scene_a` / `scene_b`.** ⚠️ Same quirk as TF: no way to tell which
  is active except by querying the other and reading the error.
- The reply's trailing `ScnStatus` (`modified`/`unmodified`) tells you the scene
  is **dirty** — useful for cue automation.
- ⚠️ `ssupdatet_ex` **silently overwrites with no confirmation.**
- **On scene-change `NOTIFY`, resync everything.** A recall changes vast state
  and the console does **not** enumerate every changed parameter.

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

## Connection, keepalive, limits

**No authentication, no handshake.** Open the socket and send.

Startup sequence:
```
devinfo productname     -> OK devinfo productname "DM7"
devinfo devicename
devstatus runmode
scpmode sstype "text"                 # REQUIRED on DM7 before scene verbs
sscurrentt_ex scene_a
```

**`devstatus error` is NOT sent to DM7** by Companion:
```js
if (!['TF','DM3','DM7'].includes(config.model)) instance.sendCmd('devstatus error')
```
Nothing replaces it — DM7 simply gets no error polling. ⚠️ Whether DM7 *supports*
it is [UNPROVEN]; the exclusion is an inference from code, not documentation.
Probe it — if it works we get free fault monitoring.

### ★ Runtime firmware detection — the architectural fix

The official DME7 spec reveals verbs Companion never sends:
```
devinfo version       -> OK devinfo version "1.60"      <-- FIRMWARE
devinfo paramsetver   -> PARAMETER SET VERSION
devinfo protocolver
```
**Gate a per-firmware capability profile on these at connect time.** This is the
structural answer to "the published tables are a floor" — instead of trusting
any table, ask the desk what it is, and (if `prminfo` works) what it has.

### Keepalive — enable it for a show tool

```
scpmode keepalive 10000     # ms, must be > 1000; actual timeout +1 s
```
**[CONFIRMED-OFFICIAL]** After activation the client must send any command or a
bare LF as a heartbeat within the timeout, or the device drops the connection.

The official rationale is exactly our failure mode:

> *"When unexpected disconnection happens… device has to keep status 'connected'
> and remote controller can't establish new connection after that."*

**A crashed or unplugged client can block reconnection until the console
notices.** Companion defaults it off with a warning; for an unattended show tool
we want it **on**.

### Encoding

⚠️ Default is **ASCII**. Send `scpmode encoding utf8` if channel names may
contain non-ASCII, or names will mangle. Companion never sends this.
Backslash is the escape character: `\\` yields a literal `\`, and `\"` yields a
literal `"`. Strings are double-quoted.

### Connection limits

| Path | Limit | Source |
|---|---|---|
| DM7 Editor + StageMix | **3 devices total** (max 1 Editor, 2 StageMix) | Official |
| OSC controllers | **4** | Official (DM7 OSC spec) |
| **RCP / TCP 49280** | **UNDOCUMENTED** | — |

⚠️ **[UNPROVEN] Does an RCP client consume an Editor/StageMix slot?** No official
statement. Indirect evidence suggests **no**: on TF, Companion (an RCP client)
and TF Editor ran simultaneously, and the separate OSC allowance implies control
paths are counted separately. **Test it** — with only 3 slots this matters if the
venue runs StageMix during the show.

## Other Yamaha families (not targets)

⚠️ Per the curated-dump finding, ❌ below means **"absent from the table we
have"**, which after the TF lesson means **nothing**. Only ✅ is load-bearing.

| | DCAs | DCA assign | EQ/HPF |
|---|---|---|---|
| Rivage PM | 24 | ✅ 288×24 | ✅ extensive |
| **DM7** (target) | **24** | ✅ **120×24** | ✅ |
| DM3 | — | ❌ ⚠️ | partial |
| CL/QL | 16 | ✅ 72×16 | ❌ ⚠️ |
| TF | 8 | ❌ ⚠️ — TheatreMix ships TF support, so **it exists** | ❌ ⚠️ |

**Revised view on TF:** given TheatreMix ships TF DCA assign at fw ≥4.00, and the
TF table dates from 2024-01-16 and is demonstrably curated,
`MIXER:Current/InCh/DCA/Assign` almost certainly **exists on current TF
firmware** — the Companion author simply never added it. Running `prminfo`
against a TF would settle it in one command. CL/QL's "missing" EQ deserves the
same treatment.

Rivage and DM3 have official public OSC specs:
- [RIVAGE PM OSC v1.0.2](https://usa.yamaha.com/files/download/other_assets/5/1407565/RIVAGE_PM_osc_specs_v102_en.pdf)
- [DM3 OSC v1.0.0](https://fr.yamaha.com/files/download/other_assets/2/2063222/DM3_osc_specs_v100_en.pdf)

## ⚠️ The ledger: where the community dump is provably wrong

Proven against the official DM7 OSC Spec V1.1.0. **Prefer the official spec.**

| Parameter | Companion dump | Official V1.1.0 | Verdict |
|---|---|---|---|
| `InCh/Port/HA/Gain` | −6…66, scale **1** | −600…6600, scale **100** | 🔴 dump wrong |
| `InCh/PEQ/Band/Q` | scale **100** | scale **1000** (Q 0.1–16.0) | 🔴 dump wrong |
| `Label/Color` | `binary`, 9 values | `string`, **11 values** | 🔴 dump wrong |
| `Label/Name` | `binary`, max 64 | `string`, **max 8 chars** | 🔴 likely wrong — test |
| `PEQ/Type` | `integer` | `string` enum of 4 | 🔴 dump wrong |
| `St/DCA/Assign` | X = **12** | X = Stereo ch count (4) | 🔴 dump wrong |
| `InCh/HPF/Freq` max | 20000 (→2 kHz) | **200000** (→20 kHz) | 🔴 likely wrong |
| `Mtrx/PEQ/Band/Freq` | **missing** (idx 101) | present | 🔴 dump incomplete |
| **`PEQ/Band/Gain` scale** | InCh **1** / Mix **100** | **10** | 🔴 **all three disagree — TEST** |

## Documentation status

**For DM7 the situation is much better than for CL/QL or TF**, which have no
public spec and sit under NDA.

Official, public, current:
- **[DM7 OSC Specifications V1.1.0](https://data.yamaha.com/files/download/other_assets/5/2234295/DM7_osc_specs_V110_en.pdf)**
  (Jul 2025) — the parameter table, Tables 1–4 (fader/pan/colour/icon).
  ⚠️ Documents the *OSC* interface (write-only, UDP 49900), but the parameter
  **IDs and scaling are shared with RCP**. Use it as the parameter reference,
  not the transport.
- **[DME7 Remote Control Protocol Spec V1.1.0](https://usa.yamaha.com/files/download/other_assets/4/2230684/DME7-remote-V110_en.pdf)**
  — `prmnum`/`prminfo`/`mtrnum`/`mtrinfo`, the `devinfo` set, keepalive
  rationale, port 49280.
- [MTX/MRX/XMV RCP Spec v3.1.0](https://data.yamaha.com/files/download/other_assets/1/1144121/mtx_mrx_xmv_ex_remote_control_protocol_spec_v310_en.pdf)
  — wire format, ERROR codes, §4.6 change notification. Contains **no**
  `prminfo`.

Unofficial but load-bearing:
- [bitfocus/companion-module-yamaha-rcp](https://github.com/bitfocus/companion-module-yamaha-rcp)
  — parameter tables (curated console `prminfo` responses), `paramFuncs.js`.
  **See the ledger above before trusting any value.**

**Trap:** the BrenekH docs' example `MIXER:Current/InCh/Fader/Name` is wrong and
contradicts its own command list. The real address is `InCh/Label/Name`.

## Hardware session

`tools/dm7_probe.py <DM7_IP>` runs the whole capture. Priority if time is short:

1. **Test B** (`prmnum`/`prminfo`) — one command decides whether we ever trust a
   third-party table again.
2. **Test E** (PEQ gain scaling) — three sources disagree; 10× errors otherwise.
3. **Test F** (dynamics/delay absence) — the exact mistake class from the TF
   correction; decides phase 4's scope.
4. **Test I** (`NOTIFY` on DCA assign) — proves the product's core capture loop.

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
