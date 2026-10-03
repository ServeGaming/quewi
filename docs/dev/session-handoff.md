# Session handoff — living document

**Read this first if you are a Claude Code session picking up this repo.**
It is the running state of the collaboration between Matthew and Claude, so a
session on any computer (or a fresh conversation) continues with no gaps.

> **⚠️ YOUR JOB, EVERY SESSION:** keep this file current and push it. When you
> finish a meaningful chunk of work — a feature, a fix, a decision, a shift in
> plan — update the relevant section below, commit it, and push. Do it at
> checkpoints, not every message, but often enough that if the session ended
> right now, the next one would lose nothing. *Update protocol* (last section)
> says how.

Last updated: **2026-10-03**. Installed on Matthew's PC: **1.0.3**. Latest
release: **v1.0.3**. `main` is ahead of it with the 1.0.4 work below (not
tagged — see "Next steps").

---

## Matthew, and how he wants to work

- Owner/user, GitHub `ServeGaming` (git author `ServeGaming`). Runs shows
  (theatre, and streams — OBS/Twitch, Discord; he uses the soundboard live).
- **Ship each change as a version bump**, committed and pushed to `main` (no
  branches needed). His standing OK covers patch releases; still tell him what's
  going out. Releases also update the docs site — see the release checklist.
- **Be honest about state**: what's tested, what's only compiled, what's
  "built but not driven". Verify by driving the real app, not just compiling.
- All `ctest` suites must stay green and `quewi.exe --selftest` must exit 0.
- Design/theme work goes to Fable (Agent tool, `model: fable`) as a background
  task with strict file boundaries. Don't propose reskins — the warm-dark,
  amber-accent aesthetic is deliberate.
- Secrets go straight to GitHub Secrets, never shown. The signing cert is fine
  and is **not** being rotated — don't raise it.
- **Never `Stop-Process` every quewi.** He keeps his own running; only stop the
  test copies you launched (match on their folder path). Killing his looked to
  him like a crash.
- Driving the GUI: he grants computer-use per test-copy exe when asked (he
  declined once, granted later). Ask; if he declines, say what's undriven.

## What quewi is

A Qt 6 / C++23 theatre cueing app (AGPL-3.0, github.com/ServeGaming/quewi),
aiming at QLab + TheatreMix parity. Windows MSI + portable zip, macOS DMG, Linux
AppImage, all built from a `v*` tag by `.github/workflows/release.yml`. Docs
site (MkDocs, `docs/`) deploys to GitHub Pages on every push to `main`
(`.github/workflows/docs.yml`); `mkdocs build --strict` must pass.

---

## Current state — on `main`, not yet released (→ 1.0.4)

All committed and pushed. **28 ctest suites, all green in CI on Windows,
macOS and Linux (+ ASan/UBSan, TSan) at `6dabf77`.**

### 0. Lighting triggers + OSC v5 for HeliOSC (2026-10-03) — Matthew's ask
"A lighting tab where I select portions of a song that send OSC or MIDI to
lighting when they're hit", targets **ETC Eos/Nomad and grandMA**, plus
"HeliOSC should control as much of the new stuff as possible".
- Model `audio/LightTrigger.*`: points or ranges (enter + exit actions) in
  **seconds of the song file**; actions = OSC / MIDI (note, CC, program, raw)
  / MSC / fire a cue. On `AudioCue` as undoable field `lightTriggers` (video
  cues: `sound.lightTriggers`). `TriggerTracker` is the pure timing logic
  (seeks skip points but catch ranges up, loops replay, stop sends exits,
  wall-clock aware so a UI stall isn't mistaken for a seek) — `test_light_triggers`.
- `GoEngine` runs a tracker per playing voice on a 10 ms precise timer;
  silent videos follow the picture clock; master arm `triggers/armed`
  (QSettings, Tools → Lighting Triggers Armed); Panic sends nothing more;
  a trigger can't fire its own song.
- UI (built by a subagent, merged): audio editor **Lighting** tab
  (`LightTriggersPanel`, presets for Eos/MA, Test buttons, "Send while
  previewing"), a lighting lane on the timeline (click = point, drag = range),
  Inspector "Lighting triggers…" (audio + video Sound section).
- OSC v5 (`app/MainWindowOscV5.cpp`): triggers list/set/add/clear,
  trigger/<ref>/set|remove|test, triggers/armed, notify/trigger/fired;
  cue/<n>/convert; soundboard/mic/* + query.
- Bugs found while driving it over UDP, all fixed: **default OSC
  subscriptions (`/quewi/notify/*`) never matched any two-level notify
  address** (since v0.9.20 — HeliOSC may never have received cue/state or
  playback unless it used its own pattern); `set/filePath` over OSC didn't
  decode, so the remote's first GO was silent; level/pan/seek/fx ignored
  video cues; trigger edits merged into one undo step.
- **Driven**: scripted UDP client (`tools/osc_triggers_drive.py`,
  `tools/osc_triggers_video.py`) against a `--selftest-idle` test copy on port 53000:
  marks at 0.5/1.0/2.0 s reached a fake desk at 0.45/0.96/1.95 s (sends lead
  the heard sound by ~50 ms — engine read position), enter/exit notifies,
  disarm, test, undo, mic query, video soundtrack, silent video, convert
  round trip. GUI: lane click/drag, Eos GO preset + Test, GO, and Send while
  previewing all reached a fake Eos on :8000.
- **Not done / limits**: nothing sent to a real Eos or MA yet (Matthew);
  MIDI/MSC paths unit-tested only (no MIDI port here); first play of a freshly
  loaded video was ~0.3 s late once; editor lane assumes the single region
  starts at 0 (moving/trimming regions shifts marks vs the file); the
  Inspector's Lighting button sits off-screen in a narrow Inspector.
- Docs (lighting-triggers page, OSC reference v5, `docs/dev/helios-v5-recap.md`
  for the HeliOSC session) — see the docs commit.
- `/quewi/workspace/new` **does** prompt Save/Discard when the show is dirty
  (the OSC reference used to say it didn't); kept the prompt (safer), fixed
  the doc.

### 1. Video cues play their sound + convert to/from audio cues (2026-10-03)
Matthew's show has every song mix as .mov / .mp4 video cues, which played
**silent** — `VideoLayer` hard-muted the player and the "route through the
AudioEngine" plan was never built.
- `VideoCue` now owns an embedded `audio::AudioCue` (`sound()`), parented to
  it, never in a list. Its file and loop follow the video's. Edited via
  `setField("sound.<field>")` (undoable like any field) and `"soundEnabled"`.
  **New video cues: sound on. Shows saved before this: sound off** (payload
  without `soundEnabled`), so old shows play exactly as before.
  `VideoCue::audioOf(cue)` = the AudioCue a cue plays through (AudioCue itself
  or an enabled video's sound) — used by every voice→cue lookup (GoEngine,
  ActiveCuesPanel, MainWindow OSC notifications, prewarm).
- GoEngine: audio firing extracted to `fireSound(audioCue, route, owner)`; the
  video branch fires the picture then `fireSound(vc->sound())`. Stop / Pause
  cue / Start-resume / Fade (param "gainDb" on a video target) / the
  Inspector scrubber's seek & play-pause all drive both.
- Inspector: a **Sound** section on video cues (play-sound checkbox, Level,
  Pan, Fade in/out, Output, **Edit sound…** → audio editor on the embedded
  cue, **Convert to audio cue**). Audio section gets **Convert (back) to video
  cue** when `CueConvert::canConvertToVideo`.
- `video/CueConvert` — `videoToAudio` / `audioToVideo`: keep the cue id (so
  Fade/Start/Stop targets and links still resolve) and identity fields; the
  audio cue stores the whole video payload in `AudioCue::videoOrigin` so going
  back restores screen/geometry/opacity; sound edits made as audio carry back.
  Any audio cue whose file is a video container can convert too.
  `core::ReplaceCueCommand` swaps the cue in place (undoable).
  `MainWindow::convertCue` stops the cue first, then pushes the command; also
  **Cue → Convert Video ↔ Audio**.
- Video soundtracks are prewarmed (decoded ahead) like audio cues — without it
  the first GO was silent ("audio still decoding"). Found by driving.
- **Driven in the real app** (test copy, synthetic `test-song.mp4` made with
  Matthew's ffmpeg at `c:\users\matth\videos\ffmpeg\bin`): video cue plays its
  sound at the set level (ACTIVE strip showed it at −20 dB), convert → audio →
  video round trip kept the level and Opacity 0.50. `test_video_sound` covers
  the model, conversion, undo, and decoding AAC out of .mp4/.mov (that case
  needs `QUEWI_TEST_VIDEO_FILE`; skips in CI).
- Known limits: picture and sound start together but run on separate clocks
  (fine for song mixes; a long take may drift for lip-sync). One unexplained
  moment while driving: an Opacity edit typed then Tab'd away didn't save
  before the first convert; couldn't reproduce (Return worked). Watch for it.

### 2. Double-click the Inspector's divider → fit to content (2026-10-03)
`MainWindow::event` catches the double-click on QMainWindow's dock separator
(it's not a widget) within 8 px of the dock edge and `resizeDocks` to
`Inspector::contentWidthHint()` (cue list keeps ≥ 360 px). **Driven: works**
(the clipped Browse / Color… / Reset buttons all came into view). Same pass
fixed a stray "Text size" label showing on video cues (`m_visualForm`
`setRowVisible`).

### 3. Soundboard "Send to mic" (2026-09-30) — needs Matthew to try
Voicemod-style: pads play into a virtual cable apps use as a microphone.
- Engine: `VoiceParams::mirrorDeviceId/mirrorGainDb` → a hidden linked copy of
  a voice on a second device (stop/fade/pause/seek/gain/pan/loop/fx follow it;
  not reported in activeVoices/voiceFinished; a missing device = no mirror,
  never a fallback to default). `AudioEngine::setLiveInput(in, out, dB)` mixes a
  captured mic into that output via an SPSC ring in its Mixer (20 ms cushion,
  skip past 80 ms backlog, re-prime on underrun); that device runs a 40 ms sink
  buffer. GoEngine takes `AudioRoute` (output, gain offset, mirror, mirror gain).
- UI: `ui/MicRouting` (QSettings `soundboard/mic/*`, per computer) +
  `MicRoutingDialog` behind a "Mic: …" soundboard button;
  `MainWindow::fireSoundboardCue` / `applyMicPassthrough`.
- Tests: live-input ring (test_mixer); mirror bookkeeping on two real devices.
  **Not driven**: no virtual cable on his PC (Voicemod driver present but its
  endpoints inactive). He needs to install VB-Audio Virtual Cable and try it.

### 0b. Lighting triggers, round 2 (2026-10-03) — simple mode, desk, beat grid
Matthew can't write OSC by hand, uses ETC Nomad (on this PC, OSC UDP RX
**8500**) / Ion; HeliOSC is for programming/busking, quewi optionally runs the
show — they don't run at once, so quewi's own OSC port (he set 8500 too,
clashing with Nomad) is fine as is (his call).
- **Simple mode** (`TriggerAction::Kind::Desk`, `DeskDo`): GO, GO on cue list,
  Stop, Back, Go to cue, sub level, bump sub, fader level, bump fader, macro,
  desk command. `audio/DeskCommands` builds the wire format — Eos per ETC's
  Show Control guide + HeliOSC's proven patterns (keys press+release 50 ms,
  stop/back keys, fader bank slot 9 so HeliOSC's slot 1 isn't re-paged,
  `/eos/newcmd`); "GO on cue list" = `Go_CueList <n> Enter` (no OSC address
  in ETC's dictionary — **unverified on a desk**); MA3 `/cmd` Go+/Go-/Goto;
  MA2 MSC. Editor: "What should it do?" + Custom OSC/MIDI toggle.
- **Lighting desk** = `core::LightingDesk`, per computer in QSettings
  `lighting/desk/*`, edited in Fable's **Lighting Desk** window (Tools →
  Lighting Desk…, Preferences → Lighting → Set up…). Custom OSC with no host
  goes there too. Set on his PC to Eos 127.0.0.1:8500 (Nomad).
- **Beat grid** (`audio/BeatGrid`): BPM/first beat/beats-per-bar on the cue
  (`beatGrid` field), Tap (+T key), Detect (onset autocorrelation + 1 ms
  onset fit: ±0.05 BPM, first beat ±20 ms on synthetic clicks — not tried on
  real songs), snap (Alt = off), Fill with beats (default bump sub 1, 40 % of
  a beat). UI by subagents; `tools/osc_desk_drive.py` drives desk actions.
- Not done: nothing verified on his real Nomad yet (offered); docs page not
  yet updated for simple mode / desk / beat grid / GO on list.

### 4. Audit A7 closed (2026-10-03)
The audio-thread UAF was already fixed in 0.9.91; what remained: removing the
active track while stopped left `m_activeTrack` and the effects rack on the
freed track (next Play crashed). Now QPointers + fallback to track 1; open
EQ/Compressor editors close with their effect. `test_audio_editor`; driven.

### 5. A second instance no longer eats the first one's journal (2026-10-03)
Bug: `recoverFromJournalIfPresent()` offered every `*.journal` in
`%APPDATA%\ServeGaming\quewi\journals`, including one a running quewi was
still writing — "No" deleted it (the live show lost crash protection), "Yes"
hijacked it. Fix: `show/JournalLock.*` — each instance holds a QLockFile
`<journal>.lock` from the moment it picks a journal path until `clearJournal()`
(journal removed first, then the lock released, which deletes the lock file).
Recovery uses `claimOrphanedJournals(dir)`: only journals whose lock it can
take (owner dead → stale by PID; age never counts, `setStaleLockTime(0)`),
newest first, each returned *locked* so two instances starting at once can't
both claim one; a recovered journal keeps its lock. Live journals are never
offered or deleted. Stray locks with no journal are tidied if stale.
- Test `test_journal_lock` (8 cases, incl. a child process that takes a lock
  and `_Exit`s = real crash, and two simultaneous claimants).
- **Verification honesty**: done in a Linux cloud session. The new suite
  passes there against Qt 6.4 (standalone build); the changed MainWindow TUs
  compile clean (syntax-only, `-Wall -Wextra`). The full project wasn't built
  or ctest'd there (needs Qt ≥ 6.7; download.qt.io blocked). CI then failed
  `journal_lock` on Windows Debug only: the age test couldn't open the owner's
  lock file to back-date it (Windows holds it with no sharing — itself the
  protection). Fixed in `943e3ac` (Windows branch checks it can't be opened,
  then skips); CI green on all OSes since. **Not driven** with two real quewi windows
  on Windows yet: next Windows session, run two test copies (one with a
  modified show) and confirm the second gets no prompt; then kill one with
  Task Manager and confirm the next launch offers its journal.
- Note: Matthew's installed 1.0.3 doesn't take the lock, so a 1.0.4 test copy
  still sees *his* live 1.0.3 journal as orphaned. Keep backing up the
  journals folder (see "Driving a test copy") until he's on 1.0.4.

### Next steps
1. Matthew: try lighting triggers against his Eos/MA, Send-to-mic (with
   VB-Cable), video sound on the real show (he reported video→audio convert
   "working perfectly" 2026-10-03; double-click into the editor for a
   converted video takes a couple of seconds to draw the waveform — decode,
   expected). Then **cut 1.0.4** (release checklist below). First release
   installable purely through the in-app updater.
2. Drive the journal-lock fix on Windows with two copies (§5).
3. Open audit items: A12 (output matrix sliders), A5 (waveform handles),
   V3/V4/V7–V10/V12–V14 (video), M11, and low-severity audio/UI items.
4. Queued idea: Freesound.org as a second sound-effects source (CC-licensed;
   needs an API key — decide how quewi gets/stores one).
5. Mix (TheatreMix) phases 3–8; DM7 hardware probe (see below).

---

## Release history (what each shipped)

- **1.0.0** (2026-07-17) — shipped "as is, patch as we go" (Matthew's call),
  with the updater and a live console run knowingly unverified.
- **1.0.1** (2026-09-24) — soundboard keybinds incl. system-wide
  (`GlobalHotkeys`, Windows `WH_KEYBOARD_LL`, scope Board/App/System in
  QSettings `soundboard/keyScope`); DCA GO, cue links, tear-off Inspector
  sections, DCA picker, Coffee theme; a whole-app audit (GoEngine rewrite,
  mixer fixes pinned by `test_mixer`, mix only touches the show's strips, Show
  Mode really locks, recovery before Welcome, **MSC = Ctrl+Alt+M**); long files
  decode to a memory-mapped cache (`SampleStore`; 3-h MP3: 4.1 GB → 45 MB).
- **1.0.2** (2026-09-24) — effects presets (28 chains, `audio/EffectPresets`,
  user presets in `effects/userPresets`); Distortion / Lo-Fi / Pitch Shift /
  Tremolo on a `SimpleEffect` base; effect-only edits now save (audit A8);
  rendered cues no longer double their effects (`bouncedPath`); "Update Render"
  rewrites in place; right-click empty pad → Import from URL (yt-dlp) + trim.
- **1.0.3** (2026-09-24) — the updater, fixed and proven (below).

## The updater — PROVEN on both paths (gate 3 closed)
- MSI path: Matthew's 1.0.2 → 1.0.3 in-app update worked end to end on
  2026-10-03 (UAC, quewi quit itself, msiexec OK, relaunched).
- Portable path: driven end to end with a test copy + the dev hook
  `QUEWI_UPDATE_PRETEND_VERSION=1.0.1` against the real release.
- Bugs that were behind years of "it just closes": the update prompt fired
  inside the Welcome dialog before `app.exec()` (Qt 6 `quit()` no-op) → now
  `MainWindow::runStartupChecks()` after Welcome + `quitForUpdate()` with a
  15 s `_Exit` watchdog; a **use-after-free** — the downloadFinished handler
  held the installer's path by reference while `deleteLater` ran in the confirm
  dialog's nested loop → by value + scope-guarded deletion; the portable swap
  helper was launched with `\"title\"`-mangled args and never ran →
  `setNativeArguments` + CREATE_NO_WINDOW, step-logged.
- Logs: `%APPDATA%\ServeGaming\quewi\update-{client,helper,install}.log`.

## The quewi Mix thread (TheatreMix inside quewi)
Design: `docs/dev/quewi-mix-spec.md`; protocols: `docs/dev/console-protocols.md`.
**Principle that must not erode:** quewi Mix assigns and labels DCAs but
**never recalls DCA fader levels**. Targets: Behringer X32/M32 (OSC/UDP) and
Yamaha DM7 (RCP/TCP; Matthew has one).
- Built: `X32Value`/`Dm7Value` codecs, `ConsoleLink` base, `X32Link`,
  `Dm7Link`, `MixShow`/`MixCue`, persistence, `MixGridModel`/`MixView`
  (View → Mix (DCA) grid, Ctrl+Shift+M), `ChannelEditorDialog`, DCA GO, links.
- `x32_emulator` test drives the real `X32Link` against pmaillot's emulator
  (independent implementation) — the wire is proven. The GUI view layer
  (live-cue marker, Scene Safe banner) hasn't been screen-confirmed.
- Not done: spec phases 3–8 (channel processing, positions, FX, level offsets,
  fader surface, OSC surface). **DM7 EQ blocked** on a hardware probe.

### X32 emulator (for testing)
At `C:/Users/matth/Documents/Apps/X32-Behringer`. Build with Qt's MinGW:
`C:/Qt/Tools/mingw1310_64/bin/gcc.exe -O2 -I X32lib -o X32.exe X32.c X32lib/Xsprint.c X32lib/Xdump.c -lws2_32`
(the clone has one local patch: X32.c's manual `getaddrinfo` prototype is
commented out). Run `X32.exe -i 127.0.0.1` → UDP 10023.

## Blocked on Matthew
- Try lighting triggers on a real Eos/Nomad or grandMA (OSC UDP 8000 / MIDI).
- Try Send-to-mic with a virtual cable; try video sound on the real show.
- Get on the DM7 → `tools/dm7_probe.py <IP>` (settles `prminfo`, PEQ gain
  scaling 1/10/100, mute-group polarity, dynamics on current firmware).

## Design / theme
Fable's review (`docs/dev/design-review.md`) + the Fusion fall-through fix
(global `QPalette` from `Theme::tokens()`). Audio editor retheme done
(`5fea82e`). Warm dark greys, creamy ink, one amber accent, 3/4 px radii; five
dark palettes share `quewi-dark.qss`, light is tokenised to match.

---

## Release checklist (every `v*` tag)
1. Version in the top `CMakeLists.txt` (`project(quewi VERSION x.y.z)`).
2. `src/ui/WhatsNewDialog.cpp` highlights (keep the previous release's below,
   for people skipping a version).
3. `docs/about/release-notes.md` + any changed feature page; change
   "new in x.y.z" markers as needed; `mkdocs build --strict`.
4. Build, `ctest`, `--selftest`; commit, push, `git tag -a vX.Y.Z`, push tag.
5. Watch the release run (macOS DMG's `hdiutil detach` sometimes flakes —
   `gh run rerun <id> --failed`), then `gh release edit vX.Y.Z --title
   "quewi X.Y.Z" --notes-file …` (the workflow leaves the body empty).

## Build / test / environment notes
- **Qt `C:\Qt\6.11.0\msvc2022_64`** (CI uses 6.8.3, min 6.7).
- **Build** (PowerShell — run from the repo root or the preset isn't found):
  ```powershell
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
  cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && cd /d C:\Users\matth\Documents\Apps\Qd && cmake --build --preset windows-release 2>&1"
  ```
  After adding source files: `cmake --preset windows-release` first.
- **Tests** (Bash): `cd build/windows-release && export PATH="/c/Qt/6.11.0/msvc2022_64/bin:$PATH" && ctest`.
  Qt test exes are GUI-subsystem: run one with `-o out.txt,txt` to see output.
  **Selftest**: `Start-Process quewi.exe -ArgumentList --selftest -Wait -PassThru`
  (PowerShell's `&` doesn't wait for GUI apps; `$LASTEXITCODE` lies).
- **Driving a test copy**: copy `quewi.exe` into a fresh scratchpad folder, run
  `windeployqt --release --no-translations` on it, launch it from there,
  `request_access(["quewi.exe"])` (grant is per exe path). Its windows open on
  the 2nd monitor (DELL S2719HS) → `switch_display`. Front windows with
  `scratchpad/front.ps1 -Title "<window title>" -Folder <folder>`. The test
  copy shares Matthew's QSettings and recovery journals: afterwards remove the
  journals it left (`%APPDATA%\ServeGaming\quewi\journals`) and reset
  `lastSeenVersion` / `ui\whatsNewVersion` if they moved.
  **Careful:** a second copy finds *his* live journal and asks to recover
  it — "No" deletes it. Fixed for 1.0.4+ (journal locks, §5), but his 1.0.3
  takes no lock, so until he's updated: back up the journals folder first
  and restore it.
  **Headless-ish**: `quewi.exe --selftest-idle` skips recovery + Welcome and
  stays open; with his copy on 53535 it binds OSC on **53000** — script it
  with a UDP client (`tools/osc_triggers_drive.py`, `tools/osc_triggers_video.py`).
- Commit messages with quotes: write them to a file and `git commit -F` (inline
  PowerShell here-strings with `"` break argument passing).
- moc gotcha: `\"` inside a raw string in a `Q_OBJECT` file → empty `.moc`.
- CI macOS pinned to `macos-14` (newer images drop AGL).
- Beware `QFileInfo::operator==` (two missing files compare equal) — use
  `AudioCue::sameFile`.

## Other docs
`docs/dev/work-plan.md`, `quewi-mix-spec.md`, `console-protocols.md`,
`release-1.0-plan.md`, `release-signing.md`, `design-review.md`,
`show-nodes-idea.md` (idea only).

## Update protocol
1. After a meaningful unit of work, edit the affected section(s); move things
   from "not done" to "done"; record decisions and why; note new blockers.
2. Update "Last updated" and the installed/released versions at the top.
3. Commit and **push** (`git push origin main`).
4. Keep it honest — "built but not driven" is a real state; say it.
5. Prune: when a thread closes, condense it. A tight, current doc beats an
   exhaustive rotting one.
