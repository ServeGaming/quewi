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

Last updated: **2026-10-04**. Installed on Matthew's PC: **1.0.3**. Latest
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

## Current state — released as 1.1.0 (was going to be 1.0.4)

All committed and pushed. **30 ctest suites** (all green in CI at `6dabf77`
before the video editor; with it, 30/30 + selftest green on a Linux Qt 6.11
build — see §6; CI covers Windows/macOS on the push).

### 0a4. Matrix List — sound + lights in one running order (2026-10-04, branch `feat/matrix-list`, PR, not on main)
Matthew: "grab the cue list from ETC like HeliOSC does and have that as a
new type of cue list called a Matrix List where it intermeshes the cues to
get a full view of the entire show." Built in a worktree session, shipped
as a PR (not merged, no version bump — that's for main).
- **Reading the desk** `osc/EosCueLists`: shares `EosFeedback`'s TCP 3032
  link (new `sendToDesk()` + `messageReceived` signal; no second socket).
  ETC OSC Get: `/eos/get/cuelist/count|index/<i>`, `/eos/get/cue/<l>/count|
  index/<i>`, windowed (32), stall → one retry → Partial (a partial read
  never replaces a complete one). **Checked read-only against Eos 3.3.9 on
  Matthew's PC (his Nomad was running, show "LIA 10-2-26")**: the cue's
  index is `args[0]`; the trailing `list/<a>/<b>` pages the *arguments*
  (list/0/31), not the cues — the first version got this wrong and only
  the real desk showed it; an index past the end answers
  `/eos/out/get/cue/0/0 <i>`; by-number `/eos/get/cue/<l>/<c>/<p>` answers
  `args[0] = -1`, or no args if absent. Nomad serves ~9 GETs/s whatever the
  window: list 2 (146 cues, decimals like 0.99/8.1) took ~17 s. So a
  `/eos/out/notify/cue/<l>` re-asks only the named cues by number + the
  count, and falls back to a full read on a count mismatch (renumber /
  range). **The notify format is ETC's documented one but was NOT seen
  from the real desk** (nothing was edited on his desk). Parts: parsed and
  folded under their cue, but his show has none — **parts unverified on a
  real desk**.
- **Model** `core/MatrixModel` (pure, `quewi_core`): rows of quewi cues;
  each desk cue placed by the first of: hand placement (With / After /
  Start, saved) → fired at GO by a quewi cue (desk GoToCue trigger at the
  top of a song, OSC `/eos/cue/[l/]n/fire`, `/eos/newcmd Go_To_Cue`, MSC
  GO) → hit in a song (trigger later in the song → nested "Hit" row by
  time) → by order (follows the previous desk cue's slot). Identity = list
  + number + part + UID: renumbers follow the UID; deleted cues stay as
  ⚠ missing rows; placements whose quewi cue is gone fall back to auto but
  are kept. Decision (SM-friendly): untied cues follow the *previous* desk
  cue, so one drag settles a run.
- **Persistence**: `CueList::Kind::Matrix` (holds no cues) + `matrixConfig()`
  in meta key `matrix_lists_json` `{listId: {version, sourceList, deskList,
  placements, deskCache, deskCachedAt}}`. Optional: shows without it are
  byte-identical to 1.1.0's format; 1.1.0 opening a show with one sees an
  empty normal cue list (harmless). The desk cache is updated silently (no
  "unsaved") so the matrix reads right offline.
- **UI** `ui/MatrixView` + `ui/MatrixSource` (helpers/JSON). + menu and
  View menu "Matrix List (sound + lights)"; tab `▦`; selecting it makes the
  source list the GO context (GO = normal GO, nothing fires desk cues);
  double-click / context menu = make standby; drag desk rows to place;
  right-click = put back / forget; Show Mode locks placing; detach works;
  follow-the-show scroll; colours from `showRegionColours()`. Placements are
  NOT on the undo stack (markModified like the mix grid).
- **Show Mode**: `ShowCueLine` gained `deskCues` + `deskOnly`; when the
  matrix page is up, COMING UP is the merged order (Hit rows skipped — the
  hits region covers them) and STANDBY lists its desk cues.
- **OSC**: `/quewi/query/matrix [from count list]`, `/quewi/query/matrix/
  current [before after list]` → `/quewi/reply/matrix` JSON;
  `/quewi/notify/matrix/changed`. In `app/MainWindowMatrix.cpp`.
- **Tests** (36 suites, all green; selftest 0): `eos_cue_lists` (fake desk
  speaking the real format: decimals, parts, 300 cues windowed, notify →
  patch / full re-read, stall → Partial; opt-in `QUEWI_TEST_EOS_HOST=127.0.0.1
  QUEWI_TEST_EOS_LIST=2` reads a real desk — passed against his Nomad),
  `matrix_model`, `matrix_persistence` (hand-built 1.1.0-era file),
  `matrix_view` (+ `QUEWI_RENDER_DIR` PNG; offscreen fonts render garbled —
  use the windows platform for a real look).
- **Driven**: a `--selftest-idle` test copy (OSC 53000, PID-checked; his
  quewi was on 8500) with a hand-made show, reading his running Nomad:
  `/quewi/query/matrix` returned 149 rows, list 2's 146 cues interleaved,
  LX 0.99 "fired" by an OSC cue, a missing LX 999 marked, and the desk's
  live 8.3 / pending 8.4 flagged as he worked. **Not driven**: the GUI by
  eye (no computer-use grant in that session) — drag-and-drop was only
  exercised through the model in tests; Show Mode's merged COMING UP only
  in a widget test.
- Docs: `using-quewi/matrix-list.md` (nav), OSC reference §Matrix List,
  release notes "Unreleased".
- **Follow-up (Matthew used it): line up both ways, GO Lights, a page that
  explains itself.**
  - *Lining up*: drag a quewi row onto a lighting row → that lighting cue
    is placed With the quewi cue (new mime `…matrix-quewi`). A quewi-on-quewi
    drop is refused with a reason, because quewi order = GO order and is
    never changed. Right-click offers "Line up … with…" (a searchable
    picker) and "Unlink" on either side. **Undoable now**:
    `core::SetMatrixPlacementsCommand` swaps only `placements`, never the
    desk cache. A new "LINED UP" column shows "◆ Lined up by hand" against
    fired / hit / desk-order.
  - *GO Lights* (`ui::goLightsTarget` / `goLightsAction`, `MainWindow::goLights`):
    fires LX NEXT = the desk's pending cue if it's in the matrix's list,
    else the cue after the active one, else the list's first. It sends
    `/eos/cue/<l>/<c>/fire` through `GoEngine::sendDeskAction` (now public,
    with an overload that takes a desk), i.e. UDP to the desk's OSC port,
    the same path as trigger "Go to cue". Disabled with a reason when the
    desk isn't Eos, the link isn't Live, or it's the end of the list.
    Ctrl+Shift+G (`transport.golights`, rebindable; the QAction isn't in
    a menu, so Show Mode doesn't lock it). Show Mode: `ShowSnapshot::lightsGo`
    → `smGoLights` under GO, in desk blue, PANIC still set apart.
  - *Page*: a one-line summary (source · desk list + label · count · link
    dot + host), short settings, a legend, a warn banner with a "Lighting
    Desk…" button when the desk isn't live, an empty state, NOW / LINED UP
    columns, whole-row tints plus a left edge stripe (`MatrixRowDelegate`,
    `showRegionColours()`), hits indented in lavender on a deeper row,
    38 px rows, tooltips on headers, cells and buttons.
  - Tests (matrix_view now 12 cases): lining up from the quewi side +
    undo/redo + refusal, the GO Lights target rules, **the exact datagram
    `/eos/cue/1/6.5/fire` (no args) via GoEngine to a UDP fake desk**,
    button vs row click, empty state. `QUEWI_RENDER_DIR` renders with the
    real QSS (resources.qrc linked into test_matrix_view, windows platform +
    `WA_DontShowOnScreen`). Renders checked at 1280×720 and 1920×1080:
    live, desk off, empty, Show Mode with GO Lights.
  - **Round 3 (Matthew's next feedback, 2026-10-04)**:
    - *GO Lights cut off*: fixed width 300 with the label elided (full label
      in the tooltip). Checked at his window (1280×931 logical ≈ 1600×1164
      at 125 %), 1280×720, 1920×1080 and 1100×720. The three text columns
      now stretch, so there's no sideways scroll at 1100.
    - *Back / Stop*: Back = `ui::backLightsTarget` (the cue before the
      desk's ACTIVE one in the matrix's list, fired explicitly with
      `/eos/cue/<l>/<c>/fire`; it runs with that cue's own time, not the
      desk's back time). Stop = DeskDo::Stop (`/eos/key/stop` 1.0, then 0.0
      after 50 ms). Ctrl+Shift+B; Stop has no default key. Both are also in
      Show Mode (`ShowLightsGo.back*/stop*`). Exact datagrams are tested.
    - *Wheel jumped ~60 rows*: `SmoothScroll` added 60 "px" to scrollbars
      that count **rows** (ScrollPerItem tables, which the matrix and the
      mix grid were). It now uses wheel-lines rows on per-item views, an
      opt-in `smoothScrollRows` per notch (the matrix: 3, per-pixel), and
      touchpad pixels as given. `SmoothScroll::stepFor` is tested.
    - *Desk details*: the whole 31-arg record plus the fx/actions replies,
      **read-only from his Nomad, 2026-10-04**. Mark is "M"; block is a
      lowercase "b"; link is int 0 for none, else a string "0.99" or an int
      3; curve is "0"; rate 100; times -1 when not set; actions "M1"; fx are
      ints. Scene end: his show doesn't use Eos's Scene End flag (arg 29
      always False) but names a cue "End of Trad. Hispanic", so a name
      starting "End" closes the open scene (`isSceneEndText`). Shown inline
      (runs on / links / loops / [B] [A]), with a Details toggle
      (`matrix/details` QSetting) or per row, and a chain line for
      follow/hang (forward only).
    - *Scenes*: `matrix::addScenes` adds Row::Kind::Scene headers.
      Collapsed scenes are `Config::collapsedScenes`, saved silently. JSON
      rows have `kind:"scene"` and `inScene`. From his real list 2:
      Trad. Hispanic 6–23, Brazil. Funk 25–49, Reggaeton 52–81.99,
      Brazilian Traditonal 83–90, Latin Lovers 92–105.99, Skirts
      106.99–108.99.
    - *Record from desk*: `EosFeedback::cueFired` (from
      `/eos/out/event/cue/<l>/<c>/fire` or the active cue changing, once per
      fire, never the state reported on connect), `audio::DeskRecording`
      (pure), and `ui::DeskTakeRecorder`. The audio editor's toolbar and
      Lighting tab have "● Record from desk": it starts the preview, stops
      when the song stops (not on loop or pause), then a review dialog
      offers Keep (one undo step, group "Recorded", optional beat snap) or
      Discard. Preview sends pause while recording. The remote can do it
      with `/quewi/cue/<n>/triggers/record start|stop|keep|discard` plus
      notifications (`MainWindowMatrix.cpp`). **Sub/fader bumps aren't
      recorded**: Eos only reports fader levels to the connection that
      configured that fader bank. **`/eos/out/event/cue/…/fire` was never
      seen live**: 10 minutes of passive listening to his Nomad caught no
      fires because he wasn't running cues. The active-cue-change path is
      what the fake desk proves.
    - Tests: new `desk_recording` (37 suites); matrix_view 15 cases;
      matrix_model scenes; eos_cue_lists details. Under `ctest -j6`,
      cart_view or light_triggers_ui occasionally fail on load; they pass
      in isolation (repeat ×4) and in serial runs.
    - Renders (QUEWI_RENDER_DIR, + QUEWI_TEST_EOS_HOST/LIST for a
      real-desk page): live at four sizes, details+folded, the Lighting
      tab recording, Show Mode with GO Lights/Back/Stop, and his real
      list 2.
  - **Not done / not driven**: GO Lights, Back and Stop were **never fired
    at the real Nomad** (as instructed; fake desk only). Recording wasn't
    driven by hand with a real song and real GOs. Drag-and-drop and the picker
    weren't driven by hand (no computer-use in that session), only through
    the model and the renders. The detached matrix window doesn't follow
    Show Mode's lock if it was opened before Show Mode.
### 7. Command menu + leader key (2026-10-04) — on a worktree branch, to merge
Matthew's ask: a macOS-style command menu (Spotlight/Raycast) "with that
super key like power that Omarchy has". Built on `worktree-agent-ac2cd7a9968a20b4c`
(not pushed to main; Claude merges it).
- `ui/CommandMenu.*` replaces `CommandPalette.*`: a frameless, modal,
  translucent panel over the main window (dim backdrop, click outside closes).
  **Search view** (Ctrl+K): fuzzy over every menu action + cues in the list on
  screen (stand by; Ctrl+↵ opens the editor) + lists + recent shows +
  Preferences pages (+ themes via View › Theme). **Key view** (which-key):
  one-letter categories — C Cue, L Lights, S Show, G Go to, F File, E Edit,
  V View, I List, T Tools, P Preferences, H Help — drill by letter, Backspace
  up, non-mnemonic → search, Esc closes, Space inert. Mnemonics from the menus'
  `&` accelerators, conflicts resolved deterministically and **remembered** in
  `commandmenu/mnemonics/<node>` (Preferences → Reset). Recent picks in
  `commandmenu/recent`. `ui/CommandMatch.*` is the pure matcher.
- `ui/LeaderKey.*`: the "Super". Default **`** (backtick) — single key, unused,
  far from Space/Esc. Registered with ShortcutManager as `commandmenu.leader`
  (rebindable in Keyboard shortcuts and Preferences → Command menu; the
  main window re-reads it on WindowActivate). Tap → key view; hold + letter →
  category directly, or a **pin** (`commandmenu/chords/<L>`, set with Ctrl+P
  in the search). Letters are swallowed while held, so A can't make a cue.
  Text fields: the key just types. Window-level only (editors untouched).
- Safety: GO/Panic/Pause/Fade All aren't menu-bar actions so they're never in
  it; the only cue action is stand-by. Modal, so Space/Esc shortcuts can't
  reach the window. Show Mode: `isRunSafe` keeps enabled, non-editing items
  only (File/Edit/Cue/List, list switching, recents, Preferences, editor
  secondary all gone).
- MainWindow: `showCommandMenu(bool keys, typed)` builds a `CommandContext`
  of values + callbacks (the only MainWindow change besides the leader hook
  and registering Ctrl+K as `commandmenu.open`). Preferences page "Command
  menu" (`makeCommandMenuPage`). Docs: `using-quewi/command-menu.md`,
  shortcuts page, release notes *Unreleased*.
- `test_command_menu` (20 cases, incl. `QUEWI_RENDER_DIR` PNGs over a
  stand-in main window). **Not driven in the real app on Windows** — only
  compiled + selftest; the leader's real-key path (ShortcutOverride on a
  live Win32 window, AltGr layouts) and the translucent backdrop on DWM
  are the things to try first.

### 6. Video editor (2026-10-04) — Matthew's ask: "add a video track editor"
There was none (video cues had the Inspector's scrubber + "Edit sound…").
Built a per-cue editor, the video counterpart of the audio editor — not a
multi-clip NLE.
- **Model.** A video cue's In/Out *are* its sound's trims (`VideoCue::trimIn/
  OutSeconds` forward to `sound()`; fields `trimInSeconds`/`trimOutSeconds`)
  — one value, so picture and sound can't drift. New `pictureFadeIn/
  OutSeconds` (payload keys; old shows load 0). `video/PictureTiming.h` is
  the pure start/stop/envelope math (fade-in first pass only, no fade-out
  while looping).
- **Engine bug fixed along the way:** the picture ignored trims entirely —
  a trimmed sound started at its trim while the picture started at 0.
  `VideoLayer` now seeks to In (repeated once loaded: some backends drop
  pre-load seeks; the player re-reports LoadedMedia after *every* seek, so
  that's a one-shot), pauses + `finished()` at Out, wraps a trimmed loop by
  hand (native Infinite loop only when untrimmed), and multiplies a per-frame
  `Layer::envelope()` into `drawnOpacity()` (kept apart from opacity so Fade
  cues on opacity don't fight it). `voiceParamsFor(VisualCue)` replaced the
  two duplicated param builders (GoEngine + MainWindow). Silent-video light
  triggers start at In and loop In→Out.
- **UI** `ui/VideoEditorWindow` (monitor = frame at playhead × opacity ×
  fade, dimmed outside the trim; transport; I/O/Home/End/←→/Space; fields
  for trims, loop, picture fades/opacity, sound level/fades; Edit sound… /
  Lighting… open the audio editor), `ui/VideoTimeline` (ruler, thumbnail
  lane, waveform lane, IN/OUT bars, fade handles on lane tops, Shift fine
  drag, wheel zoom), `ui/VideoThumbnailer` (own paused player; paused
  seeks deliver a frame, ~30–70 ms each; coarse pass first). Toolbar/header
  helpers moved out of AudioEditorWindow into `ui/EditorChrome` (shared).
  Edits = `EditCueFieldCommand` on the show's undo stack (drags merge).
  Opens on double-click of a video cue, Inspector **Edit video…**, **Cue →
  Edit Video…**; one window per cue (raised if open).
- `test_video_editor` (20 cases: timing math, cue fields/payload, fire
  params, timeline drags/handles/seek, editor undo/follow/close; 4 need
  `QUEWI_TEST_VIDEO_FILE` and skip in CI — they pass on a generated 12 s
  clip, and fail if the start seek / Out handling is removed).
- **Driven** (Linux, Xvfb, real app `--selftest-idle` + OSC): double-click
  opened the editor with thumbnails; click+I / click+O set In 4.00 / Out
  12.00 (frame-snapped); dragged picture fade-in 1.49 s and sound fade-out
  1.0 s (read back over OSC); Space played from In and stopped at Out;
  Ctrl+Z ×2 restored the trims; GO from the show started the picture at In
  (~0.3 s load latency, as before), faded up, and stopped at Out.
  **Not driven on Windows**, and the preview's *audio* wasn't heard (no
  sound device in the container). Docs: `cue-types/video.md` (video editor
  section), OSC field reference, 1.0.4 release notes.
- Noticed, not fixed: the theme QSS logs `Unknown color name '#2a2825Hover'`
  / `'#4a443dFocus'` at startup — token substitution replaces `bgRow` inside
  `bgRowHover` (and `outline` inside `outlineFocus`), so those rules get a
  bad colour. Pre-existing (`ui/Theme.cpp` load); fix = substitute longest
  token names first.

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

### 0c. Video editor redesign + splicing (2026-10-04)
- Remote session added the video editor (b433dde); merged. Fable redesigned
  it NLE-style (viewer + transport, one Inspector column Clip/Video/Audio/
  Lighting, V1/A1 timeline, Select/Razor tools) and built splicing.
- **Cuts** (`audio/Cuts`): AudioCue field `cuts` (video cues forward it,
  like In/Out). Mixer jumps them sample-exactly with a 3 ms dip, loops too;
  VideoLayer seeks past them per frame; triggers inside a cut don't fire.
  Editor: drag-select + Delete, Razor (B) splits, right-click Restore,
  Keep only this. Driven: cut 2-4 s, playback 1.5 s -> 5.11 s in 1.6 s.
- Space now plays/pauses from any focus in both editors (EditorChrome
  `installEditorSpaceKey`); "+ Point here"/M use the playhead while playing.
- **GO on another cue list** (Eos) = that list's fader GO
  (`/eos/fader/<bank>/<n>/fire`); **verified by Matthew on his Nomad**
  (list 2 = page 3 fader 3). Go_CueList via newcmd does NOT work.
- Fixed: theme @token substitution corrupted @bgRowHover etc. at random.
- Docs updated (2026-10-04): lighting-triggers (desk setup, simple mode,
  GO on list, custom, beat grid, fill), video.md (NLE layout, splicing, keys),
  OSC reference (`desk` kind + `do` table, lighting desk v5, `beatGrid`,
  `cuts`), shortcuts (editor Space), release notes 1.0.4.

### 0a5. Safe key, cue-list zoom, List menu; UI overhaul dropped (2026-10-04)
- **Safe key** (`ui/SafeKey`, QSettings `safety/safeKey|safeKeyForGo|safeKeyForDelete`,
  Preferences → Show Mode): hold Shift/Ctrl/Alt/any key to GO / delete. Local GO
  goes through `MainWindow::requestLocalGo` (action, transport, Show Mode, cue
  list Space); OSC GO isn't gated. A modifier safe key turns "mod+GO key" into
  GO via the app filter's `goChord`. Driven: plain click/Space blocked with
  "Hold Shift to GO", Shift+Space and Shift+click fire.
- **Cue list zoom** (`CueListView::setZoom`, `cueList/zoom`, 70–300 %):
  Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0; widget QSS font-size, model font
  scale for number/wait columns, columns scaled. Driven with Ctrl+=.
- List menu holds New cue list / Soundboard / Mix (DCA) list (the + is gone).
- The `ui-overhaul` branch (Gemini A+B combined redesign) was built, shown to
  Matthew, and **deleted at his request** — he didn't like it.
- In flight: Fable building a Spotlight-style command menu with an
  Omarchy-style leader key; Matrix List PR #1 waits for his "merge it".

### 0a3. Stage-manager Show Mode, Lighting panel, Eos read-back (2026-10-04)
Matthew asked for "next trigger in show mode" + "listen to the desk", a panel
on the main screen, and a simple SM-friendly Show Mode like HeliOSC's.
- `audio::upcomingEdges` (cut/trim aware) + `GoEngine::upcomingTriggers()`.
- `osc::EosFeedback`: TCP 3032, OSC 1.0 length framing, /eos/subscribe 1,
  /eos/ping 5 s, 12 s silence watchdog, 3 s reconnect; parses
  /eos/out/{active,pending,previous}/cue/text (label-safe parser — HeliOSC's
  took the first number as the time), /eos/out/active/cue (0..1),
  show/name, event/state (blind). `LightingDesk.feedback`/`feedbackPort`
  (checkbox in the Lighting Desk window, Eos only). MainWindow polls the
  desk setting every 2 s (`syncDeskFeedback`). query/lightingDesk has `state`.
- `ui/ShowSnapshot.h` = the data contract; MainWindow::buildShowSnapshot.
  Fable built `ui/ShowModeView` (stacked in place of the working screen in
  Show Mode; docks hide/restore; pref `showmode/stageManagerView`, default on)
  and `ui/LightingPanel` (View → Lighting panel dock, hidden by default).
- Driven in a test copy with `scratchpad/fake_eos_tcp.py` on 3032: panel Live
  with active/pending; Show Mode after GO: standby/notes/coming up, running
  -0:16 with bar, hit countdown 1.4 s to Hit 3 at 4.6 s (correct), desk card.
  **Not tried on his real Nomad** (it wasn't running). 32/32 ctest.
- INCIDENT: a setup script hit port 8500 = Matthew's own open quewi (his
  "LIA 2026-2027" show) — reverted with 10 verified /quewi/undo steps (6 cues,
  cue 1 file/notes/18 triggers, cue 2 notes all back). Always check the port
  owner (memory: osc-drive-check-port). His show still shows unsaved (*).


Matthew asked to shift-click many beats and give them the same fader bump /
duration, kept in a group for later bulk edits.
- `LightTrigger::group` (JSON `group`, omitted when empty); `triggerGroups`,
  `uniqueGroupName`; `TriggerAction::applyChange(before, after)` copies only
  the changed fields (a kind change, or a target of another kind, copies
  the whole action).
- Panel: `m_selection` + primary `m_selectedId`; ExtendedSelection list with
  a Group column; multi-mode editor (banner `ltMultiLabel`, name/times
  disabled, tristate Enabled, Group combo `ltGroup`, "Select all N");
  `clickTrigger` (plain = group, Ctrl toggle, Shift add+group, Alt one),
  `selectSpan`, `moveTriggersBy`, `deleteSelection`, `duplicateSelection`
  (block lands on next bar with a grid; copies get "<group> 2"),
  `groupSelection`. Fill with beats groups + selects what it adds.
- Lane (`TimelineCanvas`): set selection, `triggerClicked(id, mods)` replaces
  `triggerSelected`; MoveMany drag; Shift/Ctrl-drag span select; picket-fence
  names hidden when they'd overlap; group items in the context menu.
  Editor window: Ctrl+G group, Delete on the lane.
- OSC: `/quewi/cue/<n>/triggers/group/<name>/set/<field>|shift|remove`
  (name percent-decoded). `tools/osc_trigger_groups_drive.py` ALL PASS on a
  test copy (hits on time after a shift; undo restores a removed group).
- Driven in the UI: clicking a grouped beat selected all 4 + banner; one
  address edit changed all 4, loner untouched. Tests: 4 new UI + 2 model.

 (2026-10-03) — simple mode, desk, beat grid
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
- Not done: sub/fader bumps and Detect not verified on his real Nomad / real
  songs yet. (Docs done 2026-10-04.)

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
0. Review/merge the Matrix List PR (`feat/matrix-list`, §0a4), then drive
   it by eye on Windows: drag placements both ways + Ctrl+Z, Line up
   with…, GO Lights on the Nomad (Ctrl+Shift+G), Show Mode COMING UP and
   GO Lights, detach;
   edit a cue on the Nomad (label, renumber, delete, add a part) and watch
   the notify path patch the matrix.
1. Matthew: try lighting triggers against his Eos/MA, Send-to-mic (with
   VB-Cable), video sound on the real show (he reported video→audio convert
   "working perfectly" 2026-10-03; double-click into the editor for a
   converted video takes a couple of seconds to draw the waveform — decode,
   expected). Then **cut 1.0.4** (release checklist below). First release
   installable purely through the in-app updater.
2. Drive the journal-lock fix on Windows with two copies (§5), and the
   video editor on his real .mov song mixes (§6): thumbnails, I/O, fades,
   preview sound, then GO.
2b. Theme QSS token bug (§6, last bullet).
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
- **1.1.0** (2026-10-04) — lighting triggers (simple desk actions, Eos/MA3/
  MSC, beat grid, Fill with beats, multi-select + groups), stage-manager Show
  Mode + Lighting panel + Eos read-back (TCP 3032), video editor (NLE layout,
  splicing/cuts), video cues play sound + video↔audio convert, soundboard →
  mic, OSC v5. Colour-coded SM screen + gliding bars (Fable). Not yet on his
  real Nomad: desk read-back, sub/fader bumps, Detect on real songs.

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
- **Linux cloud sessions can build and run everything.** Ubuntu's Qt is 6.4
  and download.qt.io is blocked, but conda-forge works: fetch
  `https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.3.0-0.tar.bz2`,
  `micromamba create -p <dir> -c conda-forge qt6-main=6.11 qt6-multimedia=6.11
  qt6-webengine=6.11 cmake ninja cxx-compiler` (webengine is what carries
  Qt Pdf; 6.8 has none there), point `CMAKE_PREFIX_PATH`/`CC`/`CXX`/
  `LD_LIBRARY_PATH` at it, `QT_QPA_PLATFORM=offscreen` for ctest. GUI
  driving: `apt install xdotool`, run `Xvfb :99`, launch with its own `HOME`
  so settings/journals are scratch; `import -window root` screenshots.
  Never `pkill -f <path>` there — it matches your own shell; `pkill -x quewi`.
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
