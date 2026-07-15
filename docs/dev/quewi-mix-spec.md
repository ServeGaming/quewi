# quewi Mix — design spec

Status: **phase 1 in progress.** Drafted 2026-07-15.
Console protocol details live in a companion doc (`console-protocols.md`);
this file is the *what* and *why*, not the *how consoles talk*.

## Where it is

Built and tested (no UI yet — nothing is user-reachable):

| | |
|---|---|
| `mix/X32Value` | Value encodings. The pure layer where the protocol's traps live: DCA1 = bit 0, inverted EQ Q, `f=0` is −∞, 1024 vs 161 step grids, three zero-pad widths. |
| `mix/ConsoleLink` | Protocol-agnostic base. Owns the assignment cache and hands subclasses `(previous, next)` so one call serves a bitmask and a per-pair boolean. `applyCue()` mutes everything the cue doesn't name. |
| `mix/X32Link` | X32/M32 over UDP. Two-socket confirmation, `/xremote` keepalive + loss detection, Scene Safe bit 5, channel links, scene-recall resync. |
| `mix/MixShow` | Channels, actors, backups, ensembles. Compiled into `quewi_core` (like `cues/`) so `Workspace` can own one. |
| `mix/MixCue` | Per-cue DCA assignments, stored DCA-first, ensembles resolved at fire time. |
| persistence | `mix_json` + `mix_list_ids` meta keys; `"mix"` in the cue registry. Round-tripped through real SQLite. |
| `CueList::Kind::Mix` | Third variant beside `Normal` / `Soundboard`. |
| `PatchManager::Category::MixingConsole` | Model only — deliberately not in the patch editor UI until it has a field editor. |

Not started: the DCA cue grid, the fader surface, `Dm7Link`, everything from
phase 3 on.

**Tests are mutation-checked, not just green.** The DCA bit order, the
two-socket arrangement, and the dual-suite test main were each verified to
actually fail when broken — a passing test that cannot fail is worse than none.

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

**`tools/dm7_probe.py <DM7_IP>` runs the whole capture** and writes
`dm7_session.log`. All writes target Ch 1 / DCA 1 and are restored; nothing
stores a scene. Run it on a rehearsal or blank scene, not a show file.

Priority if time is short:
1. **Test B — `prmnum` / `prminfo`.** The self-description query is officially
   documented (for DME7; ⚠️ unproven on consoles). If a DM7 accepts it, we
   enumerate the live desk at connect time and **retire this entire class of
   error permanently.** One command. Do this first.
2. **Test E — PEQ gain scaling.** Three sources disagree. 10× errors otherwise.
3. **Test F — do the "absent" params exist?** Dynamics and delay are missing
   from both the dump and the official spec, yet the console has them. This is
   the exact mistake class the TF correction exposed. Decides phase 4's scope.
4. **Test I — `NOTIFY` on a DCA assign.** Proves the product's core capture
   loop, and reveals whether connecting DM7 Editor kills our socket.

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
| Transport | **UDP** 10023 | **TCP** 49280 |
| Encoding | binary OSC 1.0 | **ASCII, LF-terminated** |
| DCA membership | **8-bit bitmask on the channel**, replace-not-toggle | **one boolean per (channel, DCA)** |
| Change notify | `/xremote`, **10 s timeout, renew** | **unsolicited `NOTIFY`, always on** |
| Self-echo | ❌ **none** — needs a two-socket trick | ✅ `OK` vs `NOTIFY` distinguishes |
| DCAs | **8** | **24** |
| Input metering | ✅ `/meters/1` (96 floats) | ✅ 120 ch, 3 pickoffs |
| Discovery | ✅ `/xinfo` broadcast | ❌ none — type an IP |
| Delivery guarantee | ❌ UDP, no acks | ✅ TCP |
| Channels | 32 (every variant) | **120** / 72 on Compact |
| EQ per input | 4 bands | 4 bands (+HPF **and** LPF) |

Consequences that must shape the base class, not be bolted on later:

- **State sync is not cheap.** DM7's per-pair boolean means a full sync is
  **120 × 24 = 2880 messages**. Rate-limiting and caching are load-bearing.
- **`setDcaAssignment` must speak the caller's language** — "channel 4 belongs
  to DCAs 1 and 3" — and let each link decide the wire form. X32 needs
  read-modify-write on a mask (`new = old | 1<<(dca-1)`, DCA1 = bit 0); DM7
  sends one message per pair. Neither shape should leak upward.
- **Confirmation is per-protocol.** DM7 gets it free (`OK` = my echo, `NOTIFY` =
  someone else's — that distinction is also what makes loop-free live capture
  possible). X32 gets it only via the two-socket trick: register `/xremote` on
  socket A, send every set from socket B, and the console relays your own change
  back to A because it thinks B is a different client. Over UDP with no acks,
  **a silently-dropped mute is otherwise undetectable.** Never fire-and-forget.
- **Mute is `on`, not `mute`, on both.** `1` = unmuted. At least the inversion
  is consistent across the two.

## Deployment landmines (X32)

Found in research, and they change the *product*, not just the code. All three
need an answer in the UI before we ship, not a discovery at someone's tech.

### 1. Scene recall silently reverts our DCA assignments

The X32 Scene Safe bitmap has **bit 5 = "Groups (DCA assign, Mute group
assign)"**. If the operator — or our own scene-recall verb — recalls a scene
without bit 5 safed, **every assignment quewi Mix made is silently undone.**
Mid-show, no error.

This is exactly the class of thing that makes a tool untrustworthy. Minimum: we
check the safe bits on connect and refuse to run, loudly, until Groups is safed.
Better: we offer to set it.

### 2. Channel links cause moves the operator didn't ask for

`/config/chlink/1-2`…`/31-32`. If a pair is linked, writing one channel's fader
or mute **moves its partner**. Read `/config/chlink/*` at startup or produce
mystery double-moves nobody can explain.

### 3. Only FOUR `/xremote` clients exist, total

X32-Edit, a tablet, any Companion instance, and quewi all compete for four
slots. In a real rig those are scarce. **Failing to register must be visible** —
silently not receiving console changes is the worst possible failure mode for a
live-capture feature.

Also read before assuming behaviour: `/-prefs/dcamute`, `/-prefs/hardmute`,
`/-prefs/invertmutes`.

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

0. **Dev harness.** The DM7 hardware capture above, plus something to build
   against for each protocol. X32 is solved for free —
   [pmaillot's emulator](https://github.com/pmaillot/X32-Behringer) speaks
   FW 4.06, holds 4 `/xremote` clients like the real desk, and runs on
   `127.0.0.1:10023`. DM7 needs a mock we write from the hardware capture.
   ⚠️ The X32 emulator is a dev harness, **not a conformance oracle** — known to
   store some values without relaying them. Validate on iron before shipping.
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

- ~~**Metering on DM7.**~~ **Resolved: it works.** DM7 has full 120-channel input
  metering with three pickoffs, like TF and unlike CL/QL. Silent/clip detection
  (phase 6) is possible on **both** targets.
  ⚠️ X32 meter values are linear with headroom **up to 8.0 (+18 dBFS)** — clip
  detection must not assume ≤1.0. ⚠️ DM7's meter→dB scaling is disputed
  (`mtrinfo` declares 0–127, the table spans 0–255); calibrate with a known tone.
- 🔴 **DM7 PEQ Band Gain scaling — three sources disagree.** The community dump
  says scale 1, Yamaha's official spec says 10, and the −1800…1800 range against
  ±18.00 dB implies 100. Yamaha's own revision note says *"Corrected values for
  PEQ Band Gain"*, so they knew it was wrong and may have half-fixed it.
  **Blocks phase 4 EQ writes.** Wrong = gains off by 10×. Test E in the probe.
- **Mute polarity.** `Fader/On` (1 = unmuted) is confirmed on both. But DM7's
  `MuteGrpCtrl/On` defaults to 0 while `Fader/On` defaults to 1, implying
  *opposite* polarity, and the official spec's "0: OFF, 1: ON" is ambiguous
  about whether ON means the mute is engaged. **Undocumented. Verify — getting
  it backwards mutes the cast mid-show.**
- **Dynamics / channel delay on DM7.** Absent from both the dump *and* the
  official spec, yet the console plainly has them and the spec exposes
  `LinkParams/Dyna1|Dyna2|Delay`. Hypothesis: they live in the table's missing
  index block 122–155. **Decides how complete phase 4 can be.** Test F.
- **Console client limits.** DM7 allows only **3** Editor/StageMix devices. If
  an RCP client consumes one of those slots, the UI must say so. Evidence
  suggests it doesn't; unverified.
- **Channel count.** TheatreMix caps at 48. Is that a protocol limit, a
  workflow limit, or a licensing one? DM7 exposes **120**. Don't inherit the cap
  without knowing why it exists.

## DM7 traps that are already decided

Not open questions — findings to build against:

- **Split mode.** DM7 can run as two independent mixers, partitioning channels,
  DCAs and mute groups. **Read `MIXER:Setup/Unit/Split/On` + `DCA/StartCh` +
  `DCA/Num` before touching any DCA index**, or we corrupt a split console.
- **Don't hardcode 120.** DM7 vs DM7 Compact differ *only* in input count
  (120 vs 72). Everything else — 24 DCAs, 12 mute groups, all buses — is
  identical, so the core logic is model-independent. Probe the count.
- **Pan is not continuous.** Only **27 legal values** (steps of 5, plus ±63 at
  the ends). Quantise before sending.
- **`scpmode sstype "text"` before any scene verb**, and scene numbers are
  quoted strings (`"4.00"`), not integers.
- **Enable `scpmode keepalive`.** Yamaha's own rationale is our exact failure
  mode: a crashed client leaves the console believing it's still connected and
  **blocks reconnection**. Companion defaults it off; a show tool wants it on.
- **`scpmode encoding utf8`** if names may be non-ASCII, or they mangle.
- **Gate capability on `devinfo version` + `devinfo paramsetver`.** This is the
  structural answer to stale tables: ask the desk what it is rather than trust
  any table. If `prminfo` also works, ask it what it *has*.
- ~~**X32 DCA membership shape.**~~ **Resolved.** 8-bit bitmask on the channel,
  `/ch/NN/grp/dca`, **DCA1 = bit 0 = value 1**. The protocol doc never states
  which bit is DCA1; it was verified against real production scene files by
  correlating 22 channels' names to their bitmaps (MSB-first reads perfectly,
  LSB-first is nonsense), plus the emulator's renderer. Replace-not-toggle, so
  read-modify-write.

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
