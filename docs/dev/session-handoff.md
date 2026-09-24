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

Last updated: **2026-09-24**. 1.0.0 shipped 2026-07-17; **1.0.1 tagged
2026-09-24** (soundboard keybinds incl. system-wide, a whole-app bug audit, a
RAM fix for long files — see "1.0.1" below). Update the date whenever you touch this file.

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

### Post-1.0 mix-workflow features (built 2026-07-18)

Three features Matthew asked for after 1.0. All **compile clean, 19/19 tests
green, selftest exits 0**, and the logic is reasoned through — but **none has
been screen-driven yet** (see the driving blocker at the end of this section).
"Built + unit-verified, view layer not yet driven" — the same honest state the
mix grid is in.

- **DCA GO button in the main transport bar** (commit `7f0438d`). A second GO,
  left of the playback GO, that fires the Mix (DCA) list at the console from
  anywhere in the app. Dusty "console" blue on dark / filled blue chip on
  light, shorter than the hero GO so they're never confused. `MixView` reports
  `canFireNext()` + a tooltip and emits `mixStateChanged()`; the transport bar
  paints the result (disabled + explanatory tooltip until a console is live on a
  Mix list). Wiring in `MainWindow::buildLayout` (the transport-connect block).
- **Bidirectional cue links** (commit `437faf7`). `Cue::linkedCueId` (persisted,
  single-sided) pairs a cue with one other — the point is pairing a sound cue
  with a DCA cue so one GO fires both. Firing resolves the forward link **and**
  any cue that links back, so it works from either end. `MainWindow` is the
  coordinator (only object owning both the GoEngine and the console link): it
  hooks `GoEngine::cueFired` + `MixView::mixCueFired`, and a `m_pendingLinkFires`
  marker set breaks the A→B→A bounce **even across a pre-wait** (async fire).
  UI: a "Linked DCA cue" picker in the Inspector's common header, shown only
  when the show has mix cues. `MixView::fireCueAtConsole()` applies a specific
  cue without advancing. **This is the one most worth driving** — confirm a
  linked pair fires both ways against the emulator and does NOT loop/hang.
- **Pop-out inspector sections** (commit `7e4a704`). Each type section (Audio,
  Object Audio, Fade, Light, Visual, OSC, MIDI, MSC, Group, Wait, target) has a
  ↗ corner button that floats it into its own always-on-top window; ↙ docks it
  back to the exact spot. Qt::Window-on-a-child trick (keeps QObject parent → no
  leak). Object Audio (nested in the audio group) is included per Matthew's ask.
  Interaction note to verify: `rebuild()` still toggles group visibility per cue
  type, so a floated section hides when you select a cue that doesn't use it and
  reappears when you select one that does — intended, but eyeball it.

**Driving blocker (why none is screen-driven yet):** computer-use only allows
the **installed** binary (`c:\program files\quewi\bin\quewi.exe` = 1.0.0),
which doesn't have these changes; the dev build at `build/windows-release/`
isn't in the allowlist and can't be granted by path. Overwriting the Program
Files install needs admin/UAC, which the desktop tools can't drive. So the
verification path is: **Matthew runs the next build/patch** (or OKs a temporary
binary swap), then eyeball the three above. The emulator on `127.0.0.1:10023`
was up this session (the `x32_emulator` suite ran live, not skipped), so the
link-fire test is ready the moment the new binary is running.

### What's NOT done on the mix feature

- **Screen-confirm the grid "lights up" with a real channel assigned.**
  Structurally certain (once the channel editor registers a strip, `resolve()`
  keeps it → `changeFor()` returns non-empty → the cell paints green/Assigned;
  the earlier "inert" behaviour was *only* because zero channels were
  registered). Driving it on screen kept getting blocked by Windows
  `textinputhost` stealing foreground, so it wasn't visually confirmed. Low
  risk, but eyeball it: mix grid → Channels & ensembles → add "Elphaba" strip 1
  → type "Elphaba" (or "1") into a DCA cell → the cell should light up.
- ~~**No end-to-end console run.**~~ **Gate 4 closed.** `tests/test_x32_emulator.cpp`
  (suite `x32_emulator`, commit `ec9eda5`) drives the real production `X32Link`
  against pmaillot's emulator — an *independent* protocol implementation, so it
  can't rubber-stamp our own reading like the FakeX32 does. Confirms the connect
  handshake, the DCA bitmask (0b101→2→0, i.e. DCA1=bit0 verified against foreign
  iron), inverted mute polarity, and the full `applyCue` fire. Skips cleanly
  (exit 0) when nothing answers on `127.0.0.1:10023`, so CI stays green; runs for
  real when `X32.exe -i 127.0.0.1` is up (or `QUEWI_X32_HOST`). What's still
  *not* screen-confirmed is the **view layer** — the live-cue marker painting and
  the Scene Safe banner — because the protocol path is now proven but the GUI
  wasn't driven end-to-end (window z-order + `textinputhost` kept stealing
  focus). Lower priority than it was; the risky part (the wire) is verified.
- Phases 3–8 of the spec: channel processing (profiles/backup/floating spare),
  positions, FX assignments, level offsets, the fader surface, the OSC surface.
  DM7 EQ is **blocked** on a hardware test (PEQ gain scaling: 3 sources disagree
  1 vs 10 vs 100).

## 1.0.1 — tagged 2026-09-24 (commit `edb0df0`)

Matthew OK'd the release. Tag `v1.0.1` pushed; the docs site now has Release
notes, Soundboard and quewi Mix pages, and the shortcuts page was re-audited
against the code. The GitHub release body is set by hand with `gh release edit`
(the workflow leaves it empty). **Next: Matthew clicks File → Check for
updates… from 1.0.0 — this is the updater's live test; if it fails, read
`%APPDATA%/quewi/update-client.log` and fix in 1.0.2.**

What went into it:

**22/22 ctest suites green, `--selftest` exits 0** at the tag. Matthew has been running dev builds copied to a scratch folder;
**never `Stop-Process` every quewi** — only the dev-build path (killing his
copy looked to him like a crash).

- **Soundboard keybinds** (`CartView`, new `GlobalHotkeys`): per-pad key via
  right-click → "Set key…", conflict warnings, plus a scope combo:
  Board / App / **System** (Windows `WH_KEYBOARD_LL` pass-through hook; fires
  only while quewi is *not* foreground, so no double-fire). Setting persists in
  QSettings `soundboard/keyScope`. System scope is Windows-only; other OSes fall
  back to App. The add-layer button is now a real "+" add button.
- **Whole-app audit** (`fix(playback)`, `fix(audio)`, `fix(mix)`, `fix(ui)`
  commits): GoEngine rewrite (auto-continue honours post-wait, playhead skips
  the chain, Pause/Fade All/panic, group modes, recursion guard); mixer fixes
  (fades hold, paused voices stop, loops wrap to trim-in, Fade Out applied) —
  pinned by `test_mixer` rendering the real mixer offline; mix only touches the
  show's strips, X32 initial-sync race fixed, DM7 writes full rows when unknown,
  ensemble renames follow; Show Mode really locks, crash recovery runs before
  Welcome, cue shortcuts no longer collide (**MSC moved to Ctrl+Alt+M**),
  workspace dirty-tracking covers soundboard/mix/patch edits.
- **RAM** (`b720b2f`): files whose decoded audio > 96 MB decode into a
  memory-mapped cache (`SampleStore`, under the app cache dir, swept at start).
  Measured on a 179-min MP3: **peak working set 45 MB, was 4.1 GB**; decode
  ~47× realtime. Also fixed `QAudioDecoder::duration()` being read as µs (it's
  ms) — the pre-reserve was 1000× too small for every file.
  `test_long_file_memory` is the manual benchmark (env `QUEWI_MEM_PROBE_FILE`).

**Audit items still open:** A7 (audio-editor track-removal crash), A8 (editor
effects not saved), A12 (output matrix sliders), A5 (waveform handles),
V3/V4/V7–V10/V12–V14 (video), M11, and the low-severity audio/UI items.

**Not yet driven by Matthew:** system-wide soundboard keys, draggable inspector
sections, DCA picker, the long-file RAM fix inside the running app.

**1.0.2 tagged 2026-09-24** (Matthew: "get it to the release"). Contents below.
The 1.0.1 → 1.0.2 update still runs 1.0.1's buggy quit, so Matthew may have
to close quewi by hand once; 1.0.2 → 1.0.3 is the real test of the quit fix
(check `update-client.log` for the new `quitForUpdate:` lines).
**In 1.0.2:** the updater quit fix (see "Updater"
below); right-click an empty soundboard pad → Choose sound file… / Import
from URL… (the Ctrl+U yt-dlp importer in pad mode: audio only, "Download to
pad", queued signal → `MainWindow::importToPad`); an "open in the audio
editor afterwards to trim" checkbox in the importer (default on for pads,
off for the cue list, remembered separately); the importer's docs page is
now in the site nav (docs now say "new in 1.0.2"). Menu + signal covered by `test_cart_view`; the download itself
was NOT driven (it hits YouTube) — Matthew to try it.
Also for 1.0.2: effects rack **presets** (`audio/EffectPresets` — 28 built-in
chains in Voice/Space/Character/Mix groups + user presets in QSettings
`effects/userPresets`), four new effects on a table-driven `SimpleEffect`
base (Distortion, Lo-Fi, Pitch Shift [two-tap delay-line], Tremolo), and three
editor bugs fixed: effect-only edits were never saved (A8 — now
`effectsEdited`/`effectsDirty`, synced to the cue 300 ms after each change);
effects played twice after a render (the render bakes the rack; the session
now stores `bouncedPath` and `AudioCue::buildEffectChain` skips the rack
while the cue plays it); re-rendering asked for a file every time (now
"Update Render" rewrites the same file via temp+swap; "Render As…" for a new
one). `AudioFile` now frees its decoder after loading so Windows lets the
render be replaced. Beware `QFileInfo::operator==`: it calls two missing
files equal — use `AudioCue::sameFile`. `test_effects` covers the DSP,
presets, dirty tracking and the bake rule. **Not driven in the GUI yet.**
**Next feature idea queued:** Freesound.org as a second source (CC-licensed
SFX; needs an API key — work out how quewi gets/stores one).

**Release checklist for every future `v*` tag** (Matthew asked explicitly):
version bump in the top `CMakeLists.txt`; WhatsNewDialog highlights; add the
release to `docs/about/release-notes.md` and update any feature page that
changed (the docs workflow deploys GitHub Pages on push to main); tag; then
`gh release edit vX.Y.Z --notes-file …` once the release workflow has made it.

## 1.0 shipped — and what that decision was

**v1.0.0 was tagged 2026-07-17 on Matthew's explicit call: "release it as is
and have patches as we go along."** Two of the four gates in
`docs/dev/release-1.0-plan.md` were open at ship time, knowingly:

- **The Windows updater is UNVERIFIED** since the 0.9.103 "download bar then
  nothing installs" report. Consequence accepted: **the first patch release
  (1.0.1) doubles as the updater's live test.** If it fails, users grab the MSI
  manually and the updater fix becomes the next patch. When cutting 1.0.1,
  watch `%APPDATA%/quewi/update-client.log` — the step-logging is already in.
- **No full quewi-against-console run at ship** — since closed at the protocol
  layer by the `x32_emulator` integration test (see the mix "NOT done" section).
  The remaining unverified sliver is the GUI view layer (marker/banner), not the
  wire.

Still explicitly deferred: code signing (paid certs; `release-signing.md`),
DM7 EQ (blocked on the hardware probe), the fader surface, and the **audio-editor
retheme** — design-review finding 3 (TimelineCanvas + ParametricEqDialog still
paint a private cool-blue palette; ~52 hardcoded colours). Handed to Fable 5 but
its run **failed on a session/usage limit before committing anything** — the tree
is clean, the retheme simply didn't happen. Purely cosmetic (the audio editor is
its own window); pick it back up when usage resets. WaveformWidget was already
retokenised in the earlier Fusion pass; CompressorDialog is the "good" reference.

## Updater (1.0.1 gate 3) — review conclusion, do NOT edit blind

Read `src/app/UpdateInstaller.cpp` in full. **It looks correct and has had
serious fix work** since the 0.9.103 report: the Windows MSI path writes an
elevated helper batch that quits quewi *first*, then runs `msiexec /i /qb!`
(basic UI — a progress bar AND visible error dialogs, unlike the old silent
`/passive` that made a failed install vanish), relaunches through Explorer at
medium integrity, and self-deletes. macOS/Linux have analogous quit-first
in-place swaps with the failure modes commented at each step. Step-logging to
`%APPDATA%/quewi/{update-client,update-helper,update-install}.log` is in.

**1.0.0 → 1.0.1 live test (2026-09-24), what the logs showed:** the install
half WORKS — once quewi had exited, the elevated helper ran msiexec, the MSI
major-upgraded 1.0.0 → 1.0.1 (only 1.0.1 left in Apps & Features) and quewi
relaunched. The bug was **quewi not closing itself**: Matthew had to close it
by hand. Root cause: the silent startup check was scheduled from the
MainWindow constructor, so its "Update available" prompt fired inside the
Welcome dialog's `exec()` — before `app.exec()` — and in Qt 6
`QCoreApplication::quit()` is a no-op until the main loop runs. The helper
then waited for a PID that never exited. Second hole: the save prompt only
came from `closeEvent` *after* the installer launched, so Cancel stranded it.

Fixed on main after the 1.0.1 tag (so it takes effect for updates *starting
from* the next release; 1.0.0/1.0.1 users updating still need to close quewi
by hand if it stays open — the helper waits ~10 min for them):
`MainWindow::runStartupChecks()` is called by main() after the Welcome dialog;
the save question is asked before launching; `quitForUpdate()` rejects open
dialogs, quits, skips the close prompt, and a 15 s detached-thread watchdog
`_Exit`s if anything still holds the process; main() returns if an update was
started before `app.exec()`. **Unexplained:** the first 1.0.0 attempt's log
stops right after "confirm answer=Yes" with no `launchInstaller()` line (the
July 0.9.120 attempt shows the same). The new code logs more steps; check
`update-client.log` after the next update.

## X32 emulator — set up and verified on this machine

pmaillot's emulator lives at `C:/Users/matth/Documents/Apps/X32-Behringer`
(sibling of the repo; cloned from github.com/pmaillot/X32-Behringer with
Matthew's approval). Build recipe that works — Qt's MinGW, **not MSVC** (the
source guards on `__WIN32__`, which MSVC doesn't define):

```
C:/Qt/Tools/mingw1310_64/bin/gcc.exe -O2 -I X32lib -o X32.exe X32.c X32lib/Xsprint.c X32lib/Xdump.c -lws2_32
```

One local patch was needed (already applied in the clone): X32.c's manual
`getaddrinfo` prototype (~line 864) conflicts with modern ws2tcpip.h and is
commented out.

Run: `X32.exe -i 127.0.0.1` → binds UDP 10023. **Verified working:** `/info`
answers `X32 Emulator / X32 / 4.06`, and a `/ch/03/grp/dca ,i 5` set/get
round-trips correctly — the first confirmation of quewi Mix's core operation
against an independent implementation of the protocol (our DCA1=bit0 mask
semantics held).

**Job now mostly done** by the `x32_emulator` integration test (`ec9eda5`),
which drives the production `X32Link` against this emulator. What's left is only
the GUI *view* layer: launch quewi → mix grid → connect to `127.0.0.1` → fire a
cue → confirm the live-cue marker paints amber and the Scene Safe banner shows.
Nice-to-have, not a gate — the protocol path is proven. Note the emulator holds
up to 4 `/xremote` clients, so a driving session can coexist with the test.

## Blocked on Matthew (things Claude cannot do)

- Updater diagnosis (softened by the ship decision — 1.0.1 will test it live,
  but a manual run of an old installed version's updater is still the fastest
  diagnosis if that fails; the log lands at `%APPDATA%/quewi/update-client.log`).
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
- ~~**Still open:** the audio editor's cool-blue palette (finding 3).~~
  **Closed 2026-07-18** (commit `5fea82e`, Fable). TimelineCanvas,
  ParametricEqDialog and the AudioEditorWindow chrome now draw every colour
  from `Theme::tokens()` — playhead on `accent`, the blue edit cursor kept on
  `info` (deliberately cool, distinct from the playhead). Colours only, verified
  no literals remain. The audio editor is now on-theme.

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
