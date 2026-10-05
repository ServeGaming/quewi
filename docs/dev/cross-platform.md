# macOS + Linux: state, how to check, what's left

Written 2026-10-05 after the first in-depth pass on the Mac and Linux builds.
Before this, both were compiled by CI and shipped (DMG, AppImage) but never
*run* by anyone. Each item below says whether it's fixed, how it was checked,
and (for the open ones) where to start. Pick any open item up in its own
session; they're independent.

## How to check a Mac or Linux build without one

- **CI** runs every test on macOS and Linux (Debug, offscreen) and now prints
  why a test failed (`--output-on-failure`). Release builds run
  `quewi --selftest` on all three OSes.
- **The release workflow smoke-tests its own packages** before attaching them:
  it mounts the DMG / runs the AppImage, runs `--selftest` (fatal), and runs the
  screenshot tour (artifact `tour-macos` / `tour-linux`). The AppImage job also
  fails if MIDI (ALSA) isn't built in. Dry run on any branch:
  `gh workflow run release.yml --ref <branch>` — it only publishes for `v*`
  tags. Then `gh run download <run-id> -n tour-macos`.
- **The screenshot tour**: `quewi --screenshot-tour <dir>` builds a demo show,
  saves a PNG of each main screen (cue list, 160 % zoom, Preferences pages,
  command menu, shortcuts, about, Lighting panel, Matrix List, Show Mode at
  1440×900 and 1280×720) and `fit.txt` — every visible button or plain label
  whose text is wider than its room, with the platform, font and DPR at the top.
  `src/app/MainWindowTour.cpp`. Self-shortening labels (ElideLabel) are skipped.
- **Linux locally (Windows host)**: a WSL distro `quewi-test` (Ubuntu 24.04,
  GCC 13, Qt 6.8.3 in `/opt/Qt`, ALSA headers) mirrors CI. Scripts in
  `C:\wsl-quewi-test\`: `build.sh [preset]` (rsyncs the worktree in and builds),
  `test.sh` (ctest offscreen + on the real WSLg X display, then `--selftest`).
  Run as `wsl -d quewi-test -u root -- bash /mnt/c/wsl-quewi-test/build.sh`.
  Remove with `wsl --unregister quewi-test`. Matthew's own `Ubuntu` (22.04) is
  the "older Linux" box for trying a released AppImage.

## Fixed in this pass (on `feat/matrix-list`, for 1.2.0)

| What a user would have hit | Fix | Checked |
|---|---|---|
| **Linux: MIDI never worked.** RtMidi quietly builds a dummy backend without ALSA headers; the 1.1.0 AppImage has no `snd_seq_open`. | `libasound2-dev` in every Linux CI/release/perf job; CMake fails on Linux without ALSA (`src/midi/CMakeLists.txt`); release smoke test fails without it. | 1.1.0 AppImage inspected (no ALSA); Linux build links it. |
| CI red since the safe-key commit: `show_mode_view`, `matrix_view` (Win/mac Debug), `eos_cue_lists` (mac). | Font-width and timing robustness, below. | 42/42 Windows; Linux offscreen + X11. |
| Show Mode: a running song's name cut to "Defying Gra…" with a wider font (macOS). | Running names wrap to 2 lines (`ShowModeView::fitStage`). | Test under offscreen (wide fonts). |
| Show Mode: "LIGHTING DESK" heading cut to "LIGHTING…" (even on Windows). | Heading fixed width; the desk link line shortens instead (tooltip has it all). | Screenshot tour `fit.txt`. |
| Matrix tests measured button labels on a never-shown window. | Tests show the view; GO Lights label check allows "…" (tooltip has the whole). | offscreen + windows. |
| `eos_cue_lists` counted signals (late replies emit too). | Waits for the result instead. | — (was mac-only flaky) |
| `progressBarGlides` lag band stricter than its own comment (150 ms). | Band matches the comment. | — (was mac-only flaky) |
| macOS: no `NSMicrophoneUsageDescription` / `NSLocalNetworkUsageDescription` (mic killed or silent; macOS 15 blocks LAN OSC/TCP to the desk). | `resources/macos/Info.plist.in` (+ `.quewi` document type, min macOS 12). | plist parses; **needs a Mac**. |
| macOS: mic opened without asking permission. | `QMicrophonePermission` request in `AudioEngine::setLiveInput` (mac only). | **needs a Mac**. |
| macOS: double-clicking a `.quewi` in Finder / dropping on the Dock did nothing. | `QFileOpenEvent` filter in `main.cpp` → same path as dragging the file onto the window (save prompt first). | **needs a Mac**. |
| macOS: projector windows (Qt::Tool panels) hid when switching apps. | `WA_MacAlwaysShowToolWindow`. | **needs a Mac + projector**. |
| macOS min version said 11; Qt 6.8 needs 12. Qt floor said 6.7; code uses 6.8 API. | 12.0 / Qt 6.8. | — |
| MacBook "delete" key (Backspace) didn't delete cues. | Delete action also on Backspace on macOS; safe-key chord too. | **needs a Mac**. |
| Safe key "Ctrl" on a Mac is ⌘ (⌘+Space = Spotlight). | Mac labels ⇧ Shift / ⌘ Command / ⌥ Option / ⌃ Control (new: physical Control = `Qt::MetaModifier`). Ctrl+Space dropped from the leader choices on Mac. | **needs a Mac**. |
| Mac/Linux: a second quewi bound the same OSC port (SO_REUSEADDR/PORT) and the two split a remote's messages. | `DontShareAddress` off Windows (Windows stays shared: Nomad on the same PC). | Linux, two instances. |
| Linux updater: AppImage in a read-only folder → helper's `mv` fails after quewi quit → nothing running. | Writability check first; helper relaunches the old one if `mv` fails. | Code only. |
| Trackpads: timeline zoom jumped ×1.25 per tiny event (and zero-delta events zoomed out); Shift-pan dead on mac; lists flew ~100 rows per swipe; ⌘-zoom kept going on momentum; no pinch. | Accumulated deltas, pixel-delta follow, momentum ignored, pinch zoom (`TimelineCanvas`, `SmoothScroll`, `CueListView`). Mouse wheels unchanged. | `test_wheel_input`, `test_cue_list_zoom`. **Feel needs a Mac trackpad.** |
| Countdown digits asked only for Windows mono fonts. | + SF Mono, Menlo, DejaVu Sans Mono, Liberation Mono. | — |

## Open — good cloud-session tasks

Each is self-contained. File:line are as of this commit.

1. **Leader key on non-US keyboards.** `LeaderKey.cpp:86,167-177` matches
   `Qt::Key_QuoteLeft` and chord letters by key code. On German/French/Spanish/
   Nordic layouts ` is a dead key (or needs Shift/AltGr), so the leader never
   arrives; chords fail on Cyrillic/Greek. Match the physical key instead
   (`QKeyEvent::nativeScanCode()` / `nativeVirtualKey()`: mac `kVK_ANSI_Grave`
   0x32 / `kVK_ISO_Section` 0x0A, X11 keycode 49, Windows scan 0x29), and map
   chord letters by physical key too. Add tests with synthetic native codes.
2. **"Ctrl" in user-facing text** should read ⌘ on a Mac: build these with
   `QKeySequence(...).toString(QKeySequence::NativeText)`. Sites:
   `CommandMenu.cpp:1161-1162`, `CueListView.cpp:301`, `MainWindow.cpp:3737,3741`,
   `MainWindowMatrix.cpp` (GO Lights hint), `MatrixView.cpp:927,1036,1520,1532,1535`,
   `AudioEditorWindow.cpp:1063`, `Inspector.cpp:191`, `PreferencesDialog.cpp:964,980`;
   `ShortcutsDialog.cpp:71` shows PortableText. Also docs/ pages say Ctrl.
3. **About on "Ctrl+?"** (`MainWindow.cpp:~1009`) — ⌘? is the mac Help search.
   Pick another (or none) on mac.
4. **Alt+drag on Linux** (`TimelineCanvas.cpp:776/882`, `VideoTimeline.cpp:834`,
   `LightTriggersPanel.cpp:1468`): KDE/XFCE/Cinnamon grab Alt+drag to move the
   window. Offer a second modifier (e.g. Meta/Super, or hold-key toggle).
5. **Art-Net out the wrong network card** (`ArtNet.cpp:14,26,71`): sends to
   255.255.255.255, which leaves by the default route on Linux/mac regardless of
   the source bind (and the bind fails once the socket auto-bound). Send to the
   chosen interface's `QNetworkAddressEntry::broadcast()`; recreate the socket
   before binding.
6. **Local Network denied → useless error.** On macOS 15 a denied Local Network
   permission makes sends fail with "No route to host". Map that
   (`QAbstractSocket::NetworkError` / EHOSTUNREACH) in OscEngine / EosFeedback /
   console links to "Allow quewi in System Settings → Privacy → Local Network".
7. **Command menu on X11 without a compositor** (`CommandMenu.cpp:895-896`,
   frameless + translucent): the backdrop paints black. Check
   `QX11Info`-free: `QGuiApplication::platformName()=="xcb"` and no compositor
   (`_NET_WM_CM_S0` owner) → draw an opaque backdrop.
8. **AppImage**: linuxdeploy comes from the unpinned `continuous` channel
   (`release.yml`) — pin a release. Only the xcb platform plugin ships (runs via
   XWayland; fine, but add `libqwayland-*` if native Wayland is wanted).
   `libOpenGL.so.0` isn't bundled (correctly — it's a GL driver lib), so a
   minimal distro needs `libopengl0`; say so in the docs' Linux install notes.
9. **Matrix tests on real TCP** (`test_matrix_view.cpp` ~173/254/602/648/682/728)
   still use real sockets + timers through fake desks; drive them through
   `feedDesk` / `setSender` (as ~345 does) to make them deterministic.
10. **`progressBarGlides`** still runs on wall-clock time; the right fix is an
    injectable clock in `ThinProgressBar` (like `EosFeedback::setClock`).

## Needs Matthew (not code)

- **Developer ID signing + notarization** for the DMG (the workflow is ready:
  `APPLE_ID` / `APPLE_TEAM_ID` / `APPLE_APP_SPECIFIC_PASSWORD` / cert secrets —
  see `release-signing.md`). Ad-hoc signing means: Gatekeeper "unidentified
  developer", Local Network / mic approvals tied to an unstable signature, and
  the in-place updater likely blocked from replacing `/Applications/quewi.app`
  by App Management on Ventura+.
- **Someone on a real Mac** for the items marked "needs a Mac" above — a
  20-minute pass: open a show by double-click, route the mic (prompt appears?),
  OSC to a desk on the LAN (Local Network prompt?), projector stays up when
  clicking another app, trackpad scroll/zoom/pinch feel, ⌘ safe key.
