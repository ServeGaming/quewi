# quewi Mix — design spec

Status: **spec / not started.** Drafted 2026-07-15.
Console protocol details live in a companion doc (`console-protocols.md`);
this file is the *what* and *why*, not the *how consoles talk*.

## Thesis

quewi does playback. TheatreMix does live mixing. They are the two halves of
one job, and today every theatre sound op runs both and bolts them together —
TheatreMix literally ships a feature whose only purpose is firing QLab cues
from the console.

quewi Mix closes that gap: **one application, one show file, both halves.**

## The principle we inherit

TheatreMix's best decision is a restraint: **it assigns and labels DCAs but
never recalls DCA fader levels.** The software does the bookkeeping — which
mics are live in this scene, on which faders, everything else muted — and the
human keeps the mix.

We copy this. Non-negotiable. A theatre mix is a performance; automating the
levels would make quewi Mix useless to the exact people it's for.

## Scope

Full TheatreMix feature parity, across Behringer/Midas **and** Yamaha, plus an
on-screen fader surface (which TheatreMix doesn't have — it requires a physical
surface).

This is a large project. See *Sequencing* for how it lands incrementally.

### Console targets — decided

| | Target | Why |
|---|---|---|
| Behringer / Midas | **X32 / M32** | Publicly documented protocol; X32-Edit and emulators speak it, so we can develop without hardware. Cheapest and most common desk in the schools/community market. |
| Yamaha | **DM7** | The user has one with regular hands-on access. |

**DM7 is the best Yamaha we could have drawn.** 24 DCAs, confirmed
`InCh/DCA/Assign` (120×24), and — unlike CL/QL and TF — **confirmed EQ and HPF**
(`InCh/PEQ/Band/*`, `InCh/HPF/Freq`). Channel profiles (phase 3) are therefore
possible on Yamaha, which looked doubtful when CL/QL was the assumed target.

Not targets: TF and CL/QL (different capability tables, and we have neither),
and anything TheatreMix doesn't support either — see *Non-goals*.

### Hardware access changes the plan

Yamaha has **no offline editor that speaks the protocol** — the CL/QL and TF
Editors are RCP clients, not servers, and don't listen on 49280. Normally that
would force Yamaha to the end of the queue and a mock-console build.

Regular DM7 access removes that constraint, so **Yamaha does not have to be
last**. It also makes one specific hour of hardware time the highest-leverage
work in the project: see *First hardware session*.

## First hardware session (DM7)

Do this **before** freezing the `ConsoleLink` base class.

The published Yamaha parameter dumps are **stale** — they're `prminfo` captures
from older firmware, and we proved they under-report (they show no DCA-assign on
TF, yet TheatreMix ships TF support at firmware ≥4.00). Absence in the dump
proves nothing. TheatreMix's firmware floors (TF ≥4.00, CL/QL ≥5.10, DM7 ≥1.52,
X32 ≥2.12) are very likely the versions where the needed parameters landed.

So: **get ground truth from the actual desk on its actual firmware** rather than
building against a table that was already out of date when we found it.

Targets for the session:
1. Dump the DM7's live parameter table (the console can self-describe — the
   published dumps *are* console responses; we need the query syntax).
2. Round-trip a DCA assignment.
3. Settle mute polarity (`Fader/On` vs `MuteMaster/On` appear to be opposite —
   undocumented).
4. Probe EQ / HPF / dynamics for real coverage.
5. Probe metering — decides whether silent/clip detection (phase 5) is possible.
6. Capture `NOTIFY` traffic from surface moves — the basis of live-edit capture.
7. Check whether an RCP client evicts DM7 Editor / StageMix.

## Architecture

The existing codebase already has the extension points this needs. Use them
rather than building a parallel show model.

### Show model

`Workspace` owns `CueList`s, `PatchManager`, `ScriptModel`, `CartGrid`. Add a
sibling:

```
Workspace::mix() -> MixShow*
```

`MixShow` owns the things that are *not* per-cue:

- **Channels** — up to 48 controlled channels. Name, colour, console strip
  number, assigned actor, backup channel, profile set.
- **DCAs** — 8–16 depending on console.
- **Ensembles** — named channel groups assigned as a unit.
- **Actors** — per-performer processing, portable across shows.
- **Profiles** — per-channel EQ/dynamics/gain/HPF presets.
- **Positions** — named acting positions (delay + pan, optional bus sends).
- **FX buses** — which buses exist and what they're called.

### Mix cues

`CueList::Kind` is already `{ Normal, Soundboard }` — a Soundboard list renders
a pad grid instead of a cue table and stays out of the set list. Add
`Kind::Mix`: renders the DCA grid, stays out of the set list, has its own GO.

**A separate list is the right call, not a compromise.** A musical has 500+ mix
cues and maybe 40 sound cues. Interleaved, the sound cues would be invisible in
a wall of DCA moves. The two lists mirror how the job is actually run.

`MixCue : cues::Cue`, `typeKey() == "mix"`. Payload:

- DCA assignment map (channel → DCA), the substance of the cue
- per-channel level offsets (−15..+5 dB)
- position / profile / FX-bus selections
- FX bus mute states
- optional snippet/scene recall

It inherits number, name, notes, colour, pre/post-wait, and the undo stack for
free.

### Console link

A console is a **patch** — named, reusable, ships with the show file. Add
`PatchManager::Category::MixingConsole` with fields for make/model/host/port.

```
class ConsoleLink : public QObject      // abstract
    connect() / disconnect() / state()
    setDcaAssignment(channel, dcaMask)
    setDcaLabel(dca, name, colour)
    setChannelMute(channel, muted)
    ... profiles, positions, sends, meters
  signals:
    surfaceChanged(...)   // console-originated change → live edit capture
    metersUpdated(...)
    connectionStateChanged(...)
```

Implementations: `X32Link`, `Dm7Link`. **The abstraction must be written against
two real protocols before it's trusted** — an interface designed around X32
alone will be an X32 interface wearing a hat. The two are about as different as
they get, which is good news for the abstraction and bad news for anyone who
designs it in a hurry:

| | X32 / M32 | Yamaha DM7 |
|---|---|---|
| Transport | **UDP** | **TCP** |
| Encoding | binary OSC | **ASCII, LF-terminated** |
| Port | 10023 (⚠️ verify) | **49280** |
| DCA membership | bitmask on the channel (⚠️ verify) | **one boolean per (channel, DCA)** |
| Change notify | `/xremote`, must be renewed | **unsolicited `NOTIFY`, always on** |
| DCAs | 8 | **24** |

Two consequences fall out of that table. A per-(channel, DCA) boolean means a
full DM7 sync is **120 × 24 = 2880 messages** — the interface cannot assume
state sync is cheap, and needs rate-limiting and caching baked in rather than
bolted on. And a bitmask vs. per-pair split means `setDcaAssignment` must be
expressed in terms the *caller* thinks in ("channel 4 belongs to DCAs 1 and 3"),
letting each link decide how to put that on the wire.

Build X32 first (it's testable without hardware), but **review the DM7 protocol
before freezing the base class.**

### Do not reuse OscEngine for the console

X32 speaks OSC over UDP, so this is tempting. It's wrong: `OscEngine` sends
from `m_udpOut` and listens on `m_udpIn`, but the X32 replies to the *source
port* of each request — replies would arrive on the outbound socket and never
be read. `ConsoleLink` owns its own socket.

Reuse `OscCodec` for encode/decode. That part is genuinely shared.

## Sequencing

Each phase is shippable. Nothing here is worth building if phase 1 isn't good.

0. **Hardware session + mock consoles.** The DM7 capture above, and a mock
   console per protocol so the other phases have something to test against in
   CI. Cheap, and everything downstream leans on it.
1. **The spine.** MixShow model, `Kind::Mix` list, MixCue, console patch,
   `ConsoleLink` + `X32Link`. DCA assign / label / auto-mute. The cue grid with
   change colours. Connect, fire cues, see faders move. *This alone is what
   people buy TheatreMix for.*
2. **DM7.** `Dm7Link` against the reviewed abstraction. Deliberately early:
   the second implementation is what proves the abstraction, and doing it while
   phase 1 is still fresh is far cheaper than retrofitting at phase 7. It also
   forces the UI to handle 24 DCAs vs 8 before that assumption calcifies.
3. **Show operation.** Live edit capture, active-cue info window, cue markers.
4. **Channel processing.** Profiles, actor settings, backup channels + floating
   spare. (Confirmed possible on DM7; would have been doubtful on CL/QL.)
5. **Design features.** Positions (delay/pan), FX assignments, FX bus mutes,
   level offsets with live capture.
6. **Monitoring & tooling.** Meter subscription, silent/clip detection,
   utilization timeline, recast find-and-replace, HTML export.
7. **On-screen fader surface.** Second monitor / touchscreen.
8. **OSC surface.** Expose mix control so HeliOSC drives it.

Moving DM7 from last to second is the main change hardware access buys us. The
old order deferred the riskiest unknown to the end, which is the wrong end.

## Open questions

- **Metering on DM7.** Decides whether phase 6's silent/clip detection is
  possible at all. CL/QL exposes no input meters; TF exposes full input
  metering. DM7 is unknown. Settle it in the hardware session.
- **Mute polarity.** `Fader/On` and `MuteMaster/On` appear to have *opposite*
  polarity on Yamaha, and it's undocumented. Verify before shipping — getting
  this backwards mutes the cast mid-show.
- **Console client limits.** If connecting evicts DM7 Editor / StageMix, the UI
  must say so. Evidence suggests RCP coexists; unverified.
- **Channel count.** TheatreMix caps at 48. Is that a protocol limit, a
  workflow limit, or a licensing one? DM7 exposes 120 inputs. Don't inherit the
  cap without knowing why it exists.
- **X32 DCA membership shape.** Believed to be a bitmask on the channel.
  Unverified pending research — and the base class depends on it.

## Non-goals

- Recalling DCA fader levels. See *The principle we inherit*.
- Consoles TheatreMix doesn't support either: X-Air, M-Air, WING Rack,
  SQ-Rack, **Qu Classic (Qu-16/24/32)**, CQ, Mackie DL, QSC TouchMix, Yamaha
  DM3, TF-Rack. These lack the DCA count or the physical surface the workflow
  needs.

  ⚠️ Note the Qu trap: TheatreMix *does* support the **new** Qu-5/6/7 (and D
  variants) at firmware ≥1.1.1, but not the older Qu Classic. Same product name,
  different desk. Don't conflate them.
- Replacing the console's own scene system. Snippet/scene recall exists so the
  console's scenes stay free for what quewi Mix doesn't cover.
