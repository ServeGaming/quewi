# Session handoff — living document

**Read this first if you are a Claude Code session picking up this repo.**
It is the running state of the collaboration between Matthew and Claude, so a
session on any computer can continue with no gaps.

> **⚠️ YOUR JOB, EVERY SESSION:** keep this file current and push it. When you
> finish a meaningful chunk of work — a feature, a fix, a decision, a shift in
> plan — update the relevant section below, commit it, and push. Do it at
> checkpoints, not every message (constant tiny edits are noise), but often
> enough that if the session ended right now, the next one would lose nothing.
> The last section, *Update protocol*, tells you exactly how.

Last updated: **2026-07-17**, after the channel/ensemble editor and the Fusion
fall-through theme pass. Update the date whenever you touch this file.

---

## What quewi is

A Qt 6 / C++23 theatre cueing app (AGPL-3.0, github.com/ServeGaming/quewi),
aiming at QLab + TheatreMix parity. Cross-platform: Windows MSI, macOS DMG
(universal), Linux AppImage, all built from a `v*` git tag by
`.github/workflows/release.yml`. The operator fires sound/video/lighting cues
live during a show, often in a dark booth under time pressure.

Owner/user: **Matthew** (GitHub `ServeGaming`, git author `ServeGaming`).
Working style he's asked for: ship each change as a version bump,
committed/pushed; "use it to the fullest"; don't worry about usage limits, he'll
say continue. Secrets go to GitHub Secrets directly, never shown here; the
signing cert is not compromised and is not being rotated — don't push on that.

## The current big thread: quewi Mix (a TheatreMix duplicate)

Matthew asked to build a **TheatreMix clone inside quewi** — live DCA mixing that
shares one cue list with playback, so nobody has to bolt two apps together.
This is the active work. Full design: `docs/dev/quewi-mix-spec.md`. Console
protocol details (X32 + Yamaha DM7): `docs/dev/console-protocols.md`.

**The one principle that must not erode:** quewi Mix assigns and labels DCAs but
**never recalls DCA fader levels.** The software does the bookkeeping (which mics
are on which faders this scene, everything else muted); the human owns the mix.
That restraint is the whole product.

**Console targets:** Behringer X32 / Midas M32 (OSC/UDP, develops against
pmaillot's emulator with no hardware) and **Yamaha DM7** (RCP/TCP, Matthew has
one with regular access). The two protocols are opposites — that's deliberate,
it's what proves the abstraction.

### What's built and working (all tested, most driven in the real app)

- `src/mix/X32Value`, `src/mix/Dm7Value` — pure value codecs. The traps live
  here (DCA1=bit0, inverted EQ Q, `f=0`=−∞, 27 legal DM7 pans, dB scaling).
- `src/mix/ConsoleLink` — protocol-agnostic base. Owns the assignment cache and
  hands subclasses `(previous, next)` so one call drives a bitmask (X32) and a
  per-pair boolean (DM7). `applyCue()` mutes every channel the cue doesn't name.
- `src/mix/X32Link` — X32/M32 over UDP. Two-socket confirmation, `/xremote`
  keepalive + loss detection, Scene Safe bit 5, channel links, scene-recall
  resync.
- `src/mix/Dm7Link` — DM7 over TCP/RCP. Diff-based pair writes, split mode,
  `OK` vs `NOTIFY`, keepalive, model-gated capabilities.
- `src/mix/MixShow` (compiled into `quewi_core`, like `cues/`) — channels,
  actors, backups, ensembles, DCA count. `src/mix/MixCue` — per-cue DCA
  assignments, stored DCA-first, ensembles resolved at fire time.
- Persistence: `mix_json` + `mix_list_ids` meta keys, `"mix"` in the cue
  registry. Round-trips through real SQLite.
- `src/ui/MixGridModel` + `src/ui/MixView` — the DCA cue grid, reachable via
  View → Mix (DCA) grid (Ctrl+Shift+M). Change-highlighting (arrival outranks
  departure), live-cue marker, warm-grey selection (not Fusion blue).
- `src/ui/ChannelEditorDialog` — the channels + ensembles editor, reached from
  the mix view's "Channels & ensembles…" button. **This is what makes the grid
  usable** — without a named channel, `resolve()` drops the strip and the grid's
  highlighting stays inert.

### What's NOT done on the mix feature

- **Screen-confirm the grid "lights up" with a real channel assigned.**
  Structurally certain (once the channel editor registers a strip, `resolve()`
  keeps it → `changeFor()` returns non-empty → the cell paints green/Assigned;
  the earlier "inert" behaviour was *only* because zero channels were
  registered). Driving it on screen kept getting blocked by Windows
  `textinputhost` stealing foreground, so it wasn't visually confirmed. Low
  risk, but eyeball it: mix grid → Channels & ensembles → add "Elphaba" strip 1
  → type "Elphaba" (or "1") into a DCA cell → the cell should light up.
- **No end-to-end console run.** The links are unit-tested against fakes, never
  against a real desk or the X32 emulator. Gate 4 (see below).
- Phases 3–8 of the spec: channel processing (profiles/backup/floating spare),
  positions, FX assignments, level offsets, the fader surface, the OSC surface.
  DM7 EQ is **blocked** on a hardware test (PEQ gain scaling: 3 sources disagree
  1 vs 10 vs 100).

## The road to 1.0

Full version: `docs/dev/release-1.0-plan.md`. 1.0 = "stable enough to run a real
show on." The pipeline can already produce installers from a `v1.0.0` tag; the
question is whether we should, and the answer is gated on:

1. **Drive the mix grid end to end** (partly done — see "NOT done" above).
2. **Channel/ensemble editor** — ✅ done.
3. **Windows updater actually installs.** Standing bug: "download bar, then
   quewi closes, nothing installs" on 0.9.103. Client step-logging shipped;
   **blocked on Matthew** running it once to produce
   `%APPDATA%/quewi/update-client.log`.
4. **One real end-to-end console run** — X32 emulator (no hardware, Claude can
   do it) or DM7 (Matthew at the desk).

Explicitly **not** 1.0 gates: code signing (ship unsigned, sign in 1.1 — it's a
paid-cert money problem, see `docs/dev/release-signing.md`), DM7 EQ, the fader
surface.

## Blocked on Matthew (things Claude cannot do)

- Run the failing Windows updater once → send `%APPDATA%/quewi/update-client.log`.
- Get on the DM7 → run `tools/dm7_probe.py <IP>`. Settles `prminfo`
  self-description (retires the stale-table error class), PEQ gain scaling,
  **mute-group polarity** (undocumented; a wrong guess mutes the cast mid-show),
  and whether dynamics exist on current firmware.

## Design / theme state

Fable 5 (the design-focused model) did a full review (`docs/dev/design-review.md`)
and a "Fusion fall-through" fix pass. Verdict: **the design is fundamentally
sound**; the theme's discipline had stopped at the QSS boundary and everything
past it drifted. Now fixed:

- The whole *class* of "beveled / Fusion-blue / foreign" bugs traced to one root
  cause: native Qt controls and `QPainter` widgets bypassing the QSS. Closed by
  a global `QPalette` built from `Theme::tokens()` (applied in `Theme::load()`),
  plus QSS rules for the gaps (radio buttons, scrollbar corner, dock title,
  table corner button). A painted widget that reads `palette()` now inherits the
  theme automatically.
- **Still open (a real design pass, not mechanical):** TimelineCanvas +
  AudioEditorWindow chrome — ~25 hardcoded cool-blue colours; the audio editor
  reads as a "different room". That's design-review finding 3.

The theme direction is deliberate and liked: warm dark greys, creamy off-white
ink, one amber accent, restrained pastels, no purple/neon/glow, 3px control /
4px panel radii. Five dark palettes share `quewi-dark.qss` and swap tokens;
`quewi-light.qss` is now tokenised to match. **Don't propose reskins** — critique
within the aesthetic.

## Other docs worth reading

- `docs/dev/work-plan.md` — the granular running to-do, with fixes/findings.
- `docs/dev/quewi-mix-spec.md` — the mix design + phase sequencing.
- `docs/dev/console-protocols.md` — X32 + DM7 protocol reference (well-sourced;
  the DM7 half has ⚠️ items pending hardware).
- `docs/dev/release-1.0-plan.md`, `docs/dev/release-signing.md`.
- `docs/dev/design-review.md` — Fable's findings (+ the Fusion pass appendix).
- `docs/dev/show-nodes-idea.md` — Matthew's idea for distributed show nodes
  (host owns the show file, other machines join as role-specific nodes). Idea
  only, not scheduled.

## Build / test / environment notes (save the next session an hour)

- **Local Qt: `C:\Qt\6.11.0\msvc2022_64`.** CI uses Qt 6.8.3. Min is 6.7.
- **Build (needs vcvars):** in PowerShell —
  ```powershell
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
  cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && cmake --build --preset windows-release 2>&1"
  ```
- **Tests (needs Qt on PATH):** in Bash —
  ```
  cd build/windows-release && export PATH="/c/Qt/6.11.0/msvc2022_64/bin:$PATH" && ctest
  ```
  Plus `./build/windows-release/quewi.exe --selftest` should exit 0.
- **Running the app for a visual check:** the desktop window z-order fights the
  app; use `SetWindowPos`/`SetForegroundWindow` via PowerShell to front it, and
  the welcome dialog blocks until dismissed. If a build link fails with "cannot
  open file 'quewi.exe'", the app is running — `Get-Process quewi | Stop-Process
  -Force` first.
- **moc gotcha (documented in `tests/test_dm7_value.cpp`):** moc treats `\"` as
  an escape *inside* a raw string literal, emits an empty `.moc`, and the only
  symptom is unresolved `metaObject` symbols. Don't put `R"(...\"...)"` in a
  `Q_OBJECT` file — use escaped literals.
- **CI:** macOS is pinned to `macos-14` (not `macos-latest`, which rolled to
  macOS 26 and drops the AGL framework Qt 6.8.3 links). If a macOS CI build fails
  with `ld: framework 'AGL' not found`, that's the runner image, not the code.
- **Working with Fable 5:** design/theme work is handed to Fable via the Agent
  tool (`model: fable`) as a background task with **strict file boundaries** so
  it never collides with concurrent Claude edits. Stage only your own files when
  both are working the tree.

## Update protocol — how to keep this current

1. When you finish a meaningful unit of work, edit the affected section(s) above.
   Move things from "not done" to "done", record decisions and their reasons,
   note anything newly blocked.
2. Update the "Last updated" date near the top.
3. Commit with a clear message and **push** (`git push origin main`) so it's on
   GitHub for the next machine. If you're holding a push for a reason (e.g. a
   background agent mid-edit), say so here and push as soon as you can.
4. Keep it honest. This document is only worth anything if it tells the truth
   about what works, what doesn't, and what's untested. "Built but not driven"
   is a real and important state — say it.
5. Don't let it sprawl. When a section goes stale or a thread closes, prune it.
   A tight, current doc beats an exhaustive rotting one.
