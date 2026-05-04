# quewi — Design Document

This is the product/UX design doc. The companion [`structure.md`](structure.md) covers code architecture.

---

## 1. Vision & non-goals

### Vision

quewi is the cueing brain at the back of the house. A stage manager opens a show file, the venue tech rig is patched, and from then on the show is a sequence of GO presses. Every press fires the right sounds, lights, projections, OSC commands, and MIDI events at exactly the right moment.

quewi targets feature-parity with QLab — the de-facto industry standard — and pushes further in two specific directions:

1. **Cross-platform.** QLab is macOS-only. quewi runs on Windows, macOS, and Linux from one codebase, because plenty of theatres run Windows or Linux rigs and shouldn't have to buy a Mac to call a show.
2. **OSC depth.** Most cue tools support a subset of OSC. quewi supports the entire OSC 1.1 specification, every type tag, every transport, pattern matching, time tags, and OSC Query — and exposes that depth in a usable UI.

### Non-goals

- **Replacing the lighting console.** Light cues in quewi fire static scenes and fades over DMX/Art-Net/sACN; a working show with complex moving lights still wants a real console (ETC EOS, grandMA, etc.) driven by quewi via OSC or MSC.
- **Replacing the audio mixer.** quewi is multi-channel matrix-aware but it's not a DAW and not a digital mixer. It plays files into a matrix and out to your interface.
- **Replacing media servers.** quewi plays video well enough for most shows, but if you're driving a 50-projector edge-blended dome, you want disguise/Watchout/Resolume — and quewi will happily talk to them via OSC.
- **Live audio effects.** No real-time pitch correction, time stretching during playback, or generative synthesis. Those belong in a DAW or Max/MSP.
- **Mobile.** The operator's instrument is a keyboard and a 24" screen. iOS/Android remotes might come later; the primary surface is desktop.

---

## 2. Performance & feel

quewi must feel *fast* — that's a design property, not just an implementation detail. Concrete budgets (asserted in CI where possible, manually otherwise):

| Metric | Target |
|---|---|
| Cold start to usable UI | < 500 ms (2020-era SSD) |
| Show file load (200 cues) | < 200 ms |
| Idle CPU (show loaded, idle) | < 0.5 % on one core |
| Idle RAM | < 150 MB resident |
| GO press → first audio sample | < 5 ms median, < 15 ms p99 |
| GO press → first OSC packet on wire | < 2 ms median |
| Cue list scroll/select | 60 fps with 10 000 cues |
| Main executable size | < 30 MB (excluding FFmpeg DLLs) |

Architectural rules that flow from these:

- Lazy-init every subsystem. Don't open audio/MIDI devices until a cue references them.
- Virtualized list views — Qt's model/view does this natively when used correctly.
- Avoid Qt Quick/QML in hot UI paths. Widgets render faster, use less RAM, and behave better under stress.
- Lock-free SPSC queues between UI thread and engine threads. No mutexes on the audio path.
- Pre-decode the next cue's audio head during pre-roll, not at GO time.
- No background threads when idle other than the audio device callback.

---

## 3. Target users

| Persona | What they need from quewi |
|---|---|
| **Stage Manager (SM)** | Press GO. Big button, no surprises, no modal dialogs during a show. Pre-flight that says "ready" or lists what's broken. |
| **Sound designer / A1** | Build sound cues with multi-channel routing, fades, loops, subgroup outputs. Inspector that shows a waveform. |
| **Lighting designer / LD** | Fire DMX scenes and fades, or send OSC to the console. Patch universes once, never again. |
| **Projection designer / video op** | Map video cues onto specific outputs, blend, fade, send to media servers via Spout/Syphon/NDI or OSC. |
| **Master electrician / ME** | Patch and pre-flight. Verify every output reaches its destination before house opens. |
| **Composer / cue programmer** | Builds the cue list, often weeks before show. Wants undo, find-replace, search, copy-paste, keyboard everything. |

---

## 4. Competitive landscape

| Tool | Platform | Strengths | Weaknesses (vs. quewi) |
|---|---|---|---|
| **QLab** | macOS only | Industry standard. Mature audio/video. Polished UX. | Mac-only. Paid Pro tiers. OSC support is good but not exhaustive. Closed source. |
| **Show Cue Systems (SCS)** | Windows | Long-established. Strong audio. | Dated UI. Windows-only. Limited OSC and video. |
| **CueServer** | Hardware | Reliable, runs without a computer. | Hardware cost. Limited per-cue richness. |
| **Companion** | Cross-platform | Free, great for OSC/MIDI dispatch. | Stream Deck-shaped, not a cue list. Not a playback engine. |
| **MultiPlay / SFX** | Windows | Free / cheap. | Audio-focused, no real video, dated UI. |
| **quewi** | Win/Mac/Linux | Cross-platform, OSC-first, fast, AGPL. | New. No track record yet. |

---

## 5. Core UX principles

1. **One screen, three panes.** Cue list (left/center), inspector (right), transport + active cues (bottom). Everything is reachable. No hidden modes.
2. **Single GO button.** Giant. Always visible. Bound to space and to a configurable hardware key. Shows the next cue's number and name in 48 pt type.
3. **Color-coded states at a glance.**
   - **White:** armed, ready
   - **Green:** running
   - **Yellow:** paused
   - **Red:** broken (file missing, output unreachable, etc.)
   - **Blue:** loaded / pre-rolled
   - **Grey:** disarmed
4. **Pre-flight panel.** Before house opens, runs through every cue: file exists? output device present? OSC destination reachable? DMX universe patched? One green "Ready" or an itemized red list with click-to-fix actions.
5. **Undo everywhere.** Full QUndoStack, even during a running show. A "freeze edits during run" toggle in Preferences for paranoid SMs.
6. **Keyboard-driven.** Every action has a shortcut. Tooltips show the binding. A command palette (Ctrl/Cmd+K) is a fuzzy search over every action.
7. **No modal dialogs during a show.** Errors during playback go to a non-blocking inbox in the bottom pane. Show Mode actively blocks any UI that would steal focus.
8. **Auto-save journal.** Every edit appends to a journal sidecar; on crash, recovery prompts to re-apply.
9. **Show Mode lock.** Operator-only mode: GO, arm/disarm, panic. Editing actions disabled, hotkeys filtered. Unlocked with a password.

---

## 6. Information architecture

```
┌────────────────────────────────────────────────────────────────┐
│  Menu bar      File · Edit · Cue · Tools · View · Window · Help │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌──────────────────────────────┐  ┌────────────────────────┐ │
│   │  Cue List                    │  │  Inspector             │ │
│   │  ──────────                  │  │  (selected cue's       │ │
│   │  1   Pre-show music   [▶]   │  │   parameters)          │ │
│   │  2   House to half    [↓]   │  │                        │ │
│   │  3   Cue 1 lights     [⚡]  │  │   Number  3            │ │
│   │  4   Door SFX         [♪]   │  │   Name    Cue 1 lights │ │
│   │  ...                         │  │   Type    Light        │ │
│   │                              │  │   Pre     0.0  Post 0  │ │
│   │                              │  │   Continue: do not     │ │
│   │                              │  │   ───────────          │ │
│   │                              │  │   [type-specific tabs] │ │
│   └──────────────────────────────┘  └────────────────────────┘ │
│                                                                 │
│   ┌────────────────────────────────────────────────────────┐   │
│   │  ▶ NEXT: 4  Door SFX           [    GO    ]   ⏸  ⏹    │   │
│   │  Active: 1 Pre-show music  [waveform meter]  -3.2 dBFS │   │
│   └────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────┘
```

- Workspace tabs along the top let one show file have multiple cue lists (typical: Main, Backup, Pre-show, Curtain Call).
- Dock layout is user-configurable; the default above is what new users see.
- A separate "Tools" menu opens auxiliary windows: OSC Monitor, MIDI Monitor, DMX Output Viewer, Audio Levels, Patch Editor.

---

## 7. Cue list interaction model

- **Selection:** single click selects, shift/ctrl extends, drag reorders.
- **Numbers:** float-valued (1, 2, 2.5, 3, 10). Auto-renumber on demand. Inserting between cues uses decimal.
- **Editing:** double-click name to rename inline. Tab moves through editable columns.
- **Search:** Ctrl/Cmd+F filters the list in place.
- **Find/replace:** Ctrl/Cmd+H operates on names, targets, OSC payloads.
- **Copy/paste:** standard, plus "duplicate cue" (Ctrl+D).
- **Drag from filesystem:** dropping a `.wav`/`.mp3`/`.mp4`/`.png` creates the appropriate cue type.
- **Group cues:** expand/collapse with arrow keys.

---

## 8. Inspector design (per cue type)

Each cue type renders a tab strip in the inspector. Common header (Number, Name, Pre-wait, Post-wait, Continue, Notes) is identical across types. Type-specific content below.

### Audio cue inspector (sketch)

```
┌─ Audio cue ───────────────────────────────────────────────────┐
│  File:   /shows/door-knock.wav         [Pick…]  [Reveal]      │
│  ┌────────────────────────────────────────────────────────┐   │
│  │  ▒▒▓▓▓▓▒▒░░░░▒▒▓▓▓▓▒▒░░  (waveform with start/end)    │   │
│  │  ▲                                ▲                    │   │
│  │  start 0:00.00              end 0:03.45                │   │
│  └────────────────────────────────────────────────────────┘   │
│  Levels  Trim  Loops  Slices  Audio Outputs  Integrations    │
│  ─────                                                        │
│   Master:  -3.0 dB ──────●─────── (slider)                    │
│   Output 1: -6.0 dB                                           │
│   Output 2: -6.0 dB                                           │
│   ...                                                         │
└───────────────────────────────────────────────────────────────┘
```

### Video cue inspector

Geometry (x, y, w, h, rotation, opacity, blend mode), surface (which output / which Spout/Syphon channel), playback (in/out, loop, rate), audio (matrix to outputs).

### Light cue inspector

Per-channel grid for the patched universes; stored as either a snapshot (every channel's value at fire time) or a delta (only channels this cue touches).

### OSC cue inspector

```
┌─ OSC cue ─────────────────────────────────────────────────────┐
│  Destination:  [Console (TCP)]  ▼     [Edit Patch…]           │
│  Address:     /eos/cue/3.5/fire                              │
│  Arguments:                                                   │
│   ┌────┬────────┬─────────────────────────────────────┐      │
│   │ #  │  Type  │  Value                              │      │
│   ├────┼────────┼─────────────────────────────────────┤      │
│   │ 1  │  int32 │  3                                  │      │
│   │ 2  │  float │  0.5                                │      │
│   │ 3  │ string │  Cue Title                          │      │
│   └────┴────────┴─────────────────────────────────────┘      │
│   [+ Add arg]   [Bundle] [Time tag]                          │
│   ──── Preview wire bytes ────                               │
│   2f 65 6f 73 2f 63 75 65 …                                  │
└───────────────────────────────────────────────────────────────┘
```

The arg type column lets the user pick any OSC 1.1 type. Bundle/time tag toggles wrap the message.

(Other cue inspectors — Fade, Group, MIDI, MSC, Network, Script — sketched in `docs/cue-types.md`.)

---

## 9. GO behavior, pre-roll, late-load, look-ahead

- **GO** advances to the next playback cue. Pre/post waits are honored. Auto-continue cues chain transparently.
- **Pre-roll:** when a cue becomes "next," the engine begins loading its assets (decoding audio head, opening file handles, validating OSC reachability). User can configure pre-roll depth (default: next 1 cue).
- **Late-load:** if the operator hits GO before pre-roll completed, the engine waits at most the cue's "max load wait" (default 100 ms) then either fires anyway (if assets reach a usable state) or fails the cue with a non-modal error.
- **Look-ahead:** the engine can pre-render the next cue's first audio buffer so the device callback emits sound on the first sample after GO.

---

## 10. Show Mode vs Edit Mode

- **Edit Mode (default):** full UI, all editing actions enabled, undo active.
- **Show Mode:** entered via menu or hotkey, optionally password-protected. UI changes:
  - Inspector is read-only (visible, but disabled).
  - Cue list reordering disabled. Selection still works for "GO at this cue."
  - File menu shows Save only; New/Open hidden.
  - All hotkeys except space (GO), escape (panic — stop all running cues with their fade-out), and cue navigation are filtered.
  - Visual indicator: a lock icon and a "SHOW MODE" banner at top.

---

## 11. Pre-flight checker rules

For each cue, validate:

- **Audio cue:** file exists, is readable, format is supported by FFmpeg.
- **Video cue:** as above; surface (output) exists.
- **Light cue:** referenced universes are patched.
- **OSC cue:** destination's host resolves and the chosen transport is reachable (UDP: send-and-pray, TCP: connect probe, WS: handshake).
- **MIDI/MSC cue:** referenced port exists.
- **Script cue:** Lua compiles.
- **Group cue:** all children pass.

Pre-flight runs on demand and on entering Show Mode. Output: "Ready" or itemized failure list, each with a "Reveal" button that selects the offending cue.

---

## 12. Keyboard shortcuts (excerpt)

Full table in [`docs/keyboard-shortcuts.md`](docs/keyboard-shortcuts.md).

| Action | Shortcut |
|---|---|
| GO | Space |
| Stop all (panic) | Esc |
| Pause all | . (period) |
| Goto cue | / |
| Select previous / next cue | ↑ / ↓ |
| Arm / disarm selected | A |
| New cue (type picker) | N |
| Delete | Del / Backspace |
| Duplicate | Ctrl/Cmd+D |
| Find | Ctrl/Cmd+F |
| Command palette | Ctrl/Cmd+K |
| Undo / Redo | Ctrl/Cmd+Z / Shift+Z |
| Show Mode toggle | Ctrl/Cmd+Shift+L |
| Save | Ctrl/Cmd+S |

---

## 13. Theming

Default: dark theme tuned for a tech booth (low blue light, high contrast text).

Built-in themes:
- **Dark** (default)
- **High contrast** — bigger fonts, AAA contrast, for accessibility
- **Color-blind safe** — replaces the green/red state colors with deuteranopia/protanopia-safe palettes (uses shape and pattern, not just color)
- **Light** — reluctantly included for daylight rehearsals

Themes are QSS files in `resources/themes/`; custom themes are user-droppable.

---

## 14. Accessibility

- Full keyboard navigation; visible focus rings always.
- Screen reader labels on every interactive element.
- Color-blind safe theme uses **shape + color** for state (✓ ▶ ⏸ ✕ ●) so state isn't color-only.
- Configurable font scaling.
- High-contrast theme meets WCAG AAA.

---

## 15. Error handling philosophy

Three classes:

1. **Programmer errors** (asserts, broken invariants) — log + crash in debug, log + recover in release.
2. **Show file errors** (corrupt file, missing asset) — non-modal banner, journal-recovery prompt, never silently lose data.
3. **Runtime errors during a show** (OSC unreachable, audio device dropped) — non-modal toast in the bottom pane's "issues" inbox; the show keeps running with the affected cue marked broken (red).

The show always keeps running. The UI never blocks GO.

---

## 16. OSC feature matrix

Tracked in [`docs/osc-coverage.md`](docs/osc-coverage.md). Headline coverage:

**OSC 1.0 baseline:**
- [x] Address pattern matching: `?`, `*`, `[chars]`, `{alt,alt}`
- [x] Type tags: `i` (int32), `f` (float32), `s` (string), `b` (blob)
- [x] Bundles with `#bundle` header and time tag
- [x] UDP transport

**OSC 1.1 / extended:**
- [x] Type tags: `h` (int64), `t` (time tag arg), `d` (double), `S` (symbol), `c` (char), `r` (RGBA color), `m` (MIDI), `T` `F` `N` `I` (true/false/nil/infinitum), `[` `]` (array)
- [x] TCP transport with SLIP framing (RFC 1055)
- [x] Pattern: `//` descendant operator
- [x] Time tags with NTP-format timestamps and immediate-execution sentinel `1`

**Beyond the spec, common-practice:**
- [x] WebSocket transport (for browser-based remotes)
- [x] Unix domain socket transport
- [x] Multicast / broadcast destinations
- [x] OSC Query (HTTP/WebSocket namespace introspection)
- [x] Live monitor (hex view + decoded view)
- [x] "Learn" mode — bind incoming patterns to cue actions

This level of coverage is rare even commercially — it's a deliberate differentiator.

---

## 17. File format goals

- **Primary:** SQLite database, single file, suffix `.quewi`. Robust against partial writes; widely tooled.
- **Sidecar:** human-readable JSON manifest exported alongside (`my-show.quewi.json`) so version control diffs make sense.
- **Forward compat:** every record carries a schema version. New fields are additive; old quewi reads new files with warnings on unknown fields, never crashes.
- **Backward compat:** quewi N reads files written by quewi N-1 forever.
- **Asset paths:** stored relative to the show file by default; absolute paths supported but flagged in pre-flight as "non-portable."

---

## 18. Future (post-1.0)

- **Networked multi-operator** — one machine drives the show, others observe / control specific cue lists. OSC under the hood.
- **Plugin / extension API** — register new cue types, output adapters, asset importers in a Lua or C++ plugin.
- **Lua scripting cue** — call into the show model and fire arbitrary logic.
- **Mobile remote** — read-only initially; web-based via the OSC-WS transport.
- **Cloud sync** for show files (optional, opt-in, encrypted).
- **Cue list versioning UI** — git-style history within a show file.
