# QLab-Parity Roadmap

A complete checklist of what QLab does and what quewi must do to match — plus the order we'll build it in. Designed to be picked up after a break: each phase is a self-contained chunk of work with a clear "done" definition.

---

## Where we are today

| Built | Status |
|---|---|
| Qt 6 + C++23 build, CMake presets, GitHub Actions CI matrix | ✅ |
| Document model: `Workspace` → `CueList` → `Cue` (polymorphic) with QUndoStack | ✅ |
| SQLite show file `.quewi` with atomic save and JSON-payload schema | ✅ |
| Cue list view with virtualised rendering, six columns, monospace numbers | ✅ |
| Inspector with common-field form and OSC-specific group | ✅ |
| Transport bar with giant GO button (Spacebar), panic, pause | ✅ |
| Memo cue (placeholder) + OSC cue (functional) | ✅ |
| Hand-written OSC 1.1 codec (every type tag, nested bundles) — UDP outbound | ✅ |
| OSC pattern matcher (`?`, `*`, `[chars]`, `{alt,alt}`, `//`) | ✅ |
| QSS dark theme matching DESIGN.md tokens | ✅ |
| Unit tests: smoke, OSC codec, OSC pattern (3/3 passing) | ✅ |

---

## What's left for QLab parity

The list below is **everything** QLab does. Items already done are checked. Items in italics are "QLab Pro only" or paid tier. quewi is AGPL so the whole list is free.

### Cue types

#### Audio
- [ ] **Audio cue** — file playback, in/out trim, fade in/out, loops, slices, master level, per-output matrix
- [ ] **Mic cue** — live audio input with monitoring level and matrix routing
- [ ] **Fade cue (audio target)** — animate level/matrix on a target audio cue
- [ ] Waveform display in the inspector (with start/end handles)
- [ ] Real-time output meters per audio cue
- [ ] *Audio FX (EQ, compression, reverb)* — QLab Pro feature

#### Video / Projection
- [ ] **Video cue** — geometry (x, y, w, h, rotation), opacity, blend mode, surface (output mapping)
- [ ] **Camera cue** — live camera input with same geometry controls as Video
- [ ] **Image cue** — static image with geometry/opacity
- [ ] **Text cue** — rendered text overlay with font/color/alignment/geometry
- [ ] **Title cue** — text with full-screen styled defaults
- [ ] **Fade cue (video target)** — animate geometry/opacity on a target video cue
- [ ] Multi-output mapping (one cue → multiple screens)
- [ ] Spout / Syphon / NDI dispatch (optional, runtime-loaded)
- [ ] Edge blending and projection mapping (corner pin, mesh warp) — *QLab Pro*

#### Lighting
- [ ] **Light cue** — DMX scene snapshot or delta over patched universes
- [ ] **Light Fade cue** — fade from current state to target over duration
- [ ] sACN (E1.31) output (hand-rolled, multicast)
- [ ] Art-Net output (hand-rolled, broadcast)
- [ ] DMX-USB output (Enttec Open DMX framing over native serial)
- [ ] Patch editor for universes → output adapters
- [ ] Light cue inspector with channel grid editor

#### Network / Control
- [x] **OSC cue** — outbound UDP (TCP/SLIP and WebSocket pending)
- [ ] **MIDI cue** — channel message (note, CC, PC, pitch bend) or sysex
- [ ] **MSC cue** — MIDI Show Control standard commands
- [ ] **Network cue (HTTP)** — method, URL, headers, body, async
- [ ] **Script cue (Lua)** — sandboxed scripting with bindings to workspace + engines

#### Timecode
- [ ] **MTC Generate** — send MIDI Timecode from quewi
- [ ] **MTC Chase** — slave to incoming MTC, locking other cues to it
- [ ] **LTC Generate** — same over audio
- [ ] **LTC Chase**
- [ ] *Timeline cue* — timeline editor with multiple cue tracks

#### Control flow
- [x] **Memo cue** — section header, no behaviour
- [ ] **Start cue** — fire a target cue
- [ ] **Stop cue** — stop a target cue
- [ ] **Pause cue** — pause a target cue
- [ ] **Load cue** — pre-roll a target cue
- [ ] **Reset cue** — reset a target cue to its initial state
- [ ] **Devamp cue** — drop out of a loop on the next iteration
- [ ] **Goto cue** — jump playhead to a target cue and continue
- [ ] **Target cue** — arm a specific cue as next without firing
- [ ] **Arm / Disarm cue** — toggle a target cue's armed state
- [ ] **Wait cue** — time delay only
- [ ] **Group cue** with five modes:
  - [ ] Parallel — fires all children on GO
  - [ ] Sequential — children act like a sub-cue list, GO advances within
  - [ ] Timeline — children placed on a timeline with offsets
  - [ ] Start first — fires only the first armed child
  - [ ] Start random — fires a random armed child
- [ ] **Cue List cue** — triggers another cue list (sub-shows)

### Engine

- [ ] **GoEngine** — real-time scheduler that owns the GO action, pre-roll, transport
- [ ] **Pre-roll** — when a cue becomes "next," pre-load assets (audio head, video first frame, OSC reachability check)
- [ ] **Look-ahead pre-render** — engine pre-renders the next cue's first audio buffer so GO emits sound on the first sample
- [ ] **Late-load handling** — if GO arrives before pre-roll completes, wait up to a configurable max then fire or fail gracefully
- [ ] **Time-tag scheduling** — OSC bundles with future time tags get held and dispatched when due
- [ ] **Auto-continue / auto-follow** — cue chains fire transparently
- [ ] **Pre-wait / post-wait** — honoured by the scheduler
- [ ] **Bus system** — Fade cues target by id; multiple fades on one target compose
- [ ] **Active-cues panel** — list of all currently running cues with progress bars and per-cue stop button
- [ ] **Lock-free SPSC ring buffers** between UI thread and audio device callback
- [ ] **Hard-real-time audio path** (no allocations, no Qt API in callback)

### Audio engine

- [ ] **Multi-channel matrix mixer** — N input channels × M output channels, per-cue
- [ ] **Audio device picker** — list/select audio interfaces
- [ ] **Per-output level control** — independent levels for each output
- [ ] **Sample-rate conversion** — handle file SR ≠ device SR
- [ ] **Streaming decode** — files larger than RAM stream from disk
- [ ] **Loop / slice playback** — loop in/out points and named slices within a file
- [ ] **Trim handles** — set in/out points without re-encoding
- [ ] **Initial choice: Qt Multimedia** for v1; **fallback to miniaudio** if latency on Windows ASIO is insufficient
- [ ] *VST/AU plugin host* — *QLab Pro feature; consider for 1.x*

### Video engine

- [ ] **FFmpeg decode pipeline** (LGPL build)
- [ ] **Qt RHI render path** to multi-output windows
- [ ] **GPU upload + texture pool** (no per-frame allocations)
- [ ] **Spout (Win) / Syphon (Mac) / NDI** runtime-loaded sharing
- [ ] **Surface management** — virtual output → display window mapping
- [ ] **Multi-display fullscreen** — one cue spans displays, or one display per cue

### Persistence

- [x] SQLite `.quewi` show file with atomic save
- [ ] **JSON sidecar manifest** for diffable version control
- [ ] **Asset path mode** — relative (default, portable) or absolute (flagged in pre-flight)
- [ ] **Schema migrations** — read old versions, optionally migrate forward
- [ ] **Auto-save journal** — every edit appends; recover on crash
- [ ] **Show file lock** — prevent two quewi instances opening the same file simultaneously

### UI

- [x] Three-pane layout (cue list / inspector / transport)
- [x] Common-field inspector
- [x] OSC inspector group
- [ ] **Inspector type registry** — each cue type registers its editor widget; replaces the hard-coded OscCue dynamic_cast in Inspector
- [ ] **Workspace tabs** — multiple cue lists per show, tabbed at top
- [ ] **Active cues panel** — bottom-of-window list of running cues with progress
- [ ] **Pre-flight panel** — validate every cue before house opens; "Ready" or itemised red list with click-to-fix
- [ ] **Show Mode lock** — operator-only mode (GO/arm/panic), password-protected, blocks editing
- [ ] **Command palette** (Cmd/Ctrl+K) — fuzzy-search every action
- [ ] **Find / replace** (Cmd/Ctrl+F, Cmd/Ctrl+H) — search names, targets, OSC payloads
- [ ] **Keyboard shortcuts** — full coverage matching docs/keyboard-shortcuts.md
- [ ] **Drag from filesystem** — drop a .wav → audio cue created automatically
- [ ] **Cue-list zebra striping & state-color row borders**
- [ ] **State-color column** in cue list (running/armed/loaded/etc. as a colored bar)
- [ ] **Cue type icons** in the type column (line icons, 14×14)
- [ ] **Color-blind safe theme**
- [ ] **High-contrast theme**
- [ ] **Light rehearsal theme**
- [ ] **Cart view** (alternative to cue list — grid of named buttons for SFX shows)
- [ ] **Theme picker** in Preferences

### Patches & device management

- [ ] **Audio output patch** — named devices with channel-count metadata
- [ ] **DMX universe patch** — universe → adapter (sACN/Art-Net/USB) mapping
- [ ] **OSC destination patch** — shared named destinations (replaces per-cue host/port)
- [ ] **MIDI port patch** — named ports with input/output flags
- [ ] **Video surface patch** — named virtual outputs → physical displays
- [ ] **Patch editor dialog** — single-window editor for all patch types
- [ ] **Patch validation** — flag misconfigurations in pre-flight

### OSC depth (already partial)

- [x] Full OSC 1.0 + 1.1 codec (every type tag, bundles)
- [x] Address pattern matcher
- [x] UDP send
- [ ] **UDP receive + dispatcher** — incoming packets routed to subscribed handlers via pattern matching
- [ ] **TCP transport** with SLIP framing per RFC 1055
- [ ] **WebSocket transport** (for browser-based remotes)
- [ ] **Unix domain socket transport**
- [ ] **Multicast / broadcast** destinations
- [ ] **OSC Query** — HTTP/WS namespace introspection so other tools can discover quewi's OSC API
- [ ] **OSC monitor window** — live hex + decoded view of incoming and outgoing packets
- [ ] **Learn mode** — bind incoming OSC patterns to cue actions
- [ ] **Dictionary import/export** — drop in a console's OSC spec

### MIDI depth

- [ ] RtMidi integration (cross-platform device I/O)
- [ ] MIDI cue (note/CC/PC/pitch bend/sysex)
- [ ] MSC encoder/decoder (full standard command set)
- [ ] MIDI controller binding to GO and panic
- [ ] MIDI monitor window

### Lighting depth

- [ ] sACN (E1.31) — packet builder + receiver
- [ ] Art-Net — packet builder + receiver
- [ ] DMX-USB serial driver
- [ ] DMX universe viewer (live channel values)
- [ ] HTP / LTP merge between cues (priority handling)

### Performance gates

- [ ] **Cold start** < 500 ms — measured in CI
- [ ] **Idle CPU** < 0.5 % / core — manual gate
- [ ] **Idle RAM** < 150 MB resident with 200-cue show — CI
- [ ] **GO → first audio sample** < 5 ms median — Phase 3 latency rig
- [ ] **GO → first OSC byte** < 2 ms median
- [ ] **Cue list scroll** 60 fps with 10 000 cues
- [ ] **Binary size** < 30 MB (excluding FFmpeg DLLs)

### Distribution & polish

- [ ] **NSIS installer** for Windows (with file association for `.quewi`)
- [ ] **Notarised .dmg** for macOS
- [ ] **AppImage** for Linux (and `.deb` / `.rpm` later)
- [ ] **Code signing** on Win + Mac (post-1.0; needs certificates)
- [ ] **Auto-update** — check GitHub releases for newer versions, prompt user
- [ ] **Crash reporter** — opt-in upload of minidump + log bundle
- [ ] **User manual** in docs/ — covers every cue type and workflow
- [ ] **Migration guide from QLab** — workspace import would be ideal but is a rabbit hole

### Beyond QLab (differentiators)

- [ ] **Cross-platform** — Windows + Linux out of the box (QLab is Mac-only)
- [ ] **OSC 1.1 + Query** — broader OSC coverage than QLab ships
- [ ] **AGPL source** — free of license fees and account gates
- [ ] **Diffable JSON sidecar** — version-control friendly
- [ ] **Networked multi-operator** — multiple quewi instances on one show, OSC under the hood (post-1.0)
- [ ] **Plugin API** — register new cue types, output adapters, asset importers (post-1.0)
- [ ] **Web remote** — read-only iPad/phone remote via the WebSocket OSC transport (post-1.0)

---

## Build order (revised phase plan)

The original 9-week phased plan still applies. Updated with what's done:

| Phase | Focus | Status |
|---|---|---|
| **0** | Scaffold (CMake, docs, CI) | ✅ done |
| **1** | App boots, document model, SQLite save/load | ✅ done |
| **2** | OSC engine (codec + pattern matcher + UDP send + OscCue) | ✅ done |
| **2.5** | OSC depth: TCP/SLIP, WebSocket, inbound dispatcher, monitor window | 🚧 next |
| **3** | Audio engine + Audio cue + Fade cue + waveform display | ⬜ |
| **4** | Lighting (sACN + Art-Net + DMX-USB) + Light/Light Fade cues + universe patch | ⬜ |
| **5** | Video / Projection (FFmpeg + Qt RHI) + Video/Image/Text/Title cues + Spout/Syphon/NDI | ⬜ |
| **6** | MIDI + MSC + control-flow cues (Start/Stop/Pause/Load/Reset/Devamp/Goto/Target/Arm/Wait) + **GoEngine** + Group cue (all 5 modes) + Inspector type registry | ⬜ |
| **7** | Polish: pre-flight, Show Mode lock, command palette, find/replace, themes (high-contrast, color-blind safe, light), drag-from-filesystem, cart view, multi-list tabs, active-cues panel, journal/recovery, performance gates in CI | ⬜ |
| **8** | Distribution: installers, auto-update, crash reporter, user manual, version 1.0 release | ⬜ |
| **9+** | Post-1.0: networked multi-operator, web remote, plugin API, Lua scripting, timecode (MTC/LTC), legacy QLab workspace import | ⬜ |

Each phase is one to two weeks of focused work. Order is dependency-driven: GoEngine in phase 6 needs the cue type catalogue from earlier phases; lighting and video can run somewhat in parallel after audio because they share the patch model but otherwise touch different output subsystems.

---

## How to resume after a break

1. Open this file. Look at the **Build order** table — find the next unchecked phase.
2. Open `UX.md` for the product/UX vision and `structure.md` for the code architecture. They've barely changed since the start; skim if it's been a while.
3. Open the latest commit in `git log` to see where we left off.
4. Tell me: "continue with phase &lt;N&gt;" or "start audio engine" or similar.
5. I'll read this roadmap, look at the current state of the code, and pick up where we stopped.

If there were any decisions or trade-offs you wanted to revisit, jot them at the bottom of this file before stepping away — the file is the durable handoff.

---

## Open decisions queued for later

These are non-blocking but will need a call before we hit them:

- **Audio engine choice for production** — Qt Multimedia (current plan) vs. miniaudio (single-header, MIT) vs. JUCE (heavy but mature). Decide after Phase 3 latency benchmark.
- **Lua sandboxing strategy** — sol2 is the integration library, but we need a sandbox so user scripts can't crash the process or leak host capabilities.
- **Multi-machine sync protocol** — proprietary OSC-based or piggy-back on something existing (RTP-MIDI, Ableton Link). Defer to post-1.0.
- **Plugin packaging** — single-file dylib? script directory? Decide when plugin API lands.
- **Video codec license** — FFmpeg LGPL build covers most needs, but ProRes / DNxHD have commercial considerations. Probably fine for AGPL distribution; double-check before 1.0 release.

---

## Notes / parking lot

(Add anything here that doesn't fit a checklist but matters for later.)
