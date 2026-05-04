# quewi — Code Structure

This is the architecture/code-structure doc. Companion: [`design.md`](design.md) for product/UX.

---

## 1. Repository layout

```
quewi/
├── CMakeLists.txt           Top-level build
├── CMakePresets.json        Per-platform configure/build presets
├── LICENSE                  AGPL-3.0
├── README.md
├── design.md                Product/UX design
├── structure.md             This file
├── .gitignore
├── .gitattributes
│
├── src/
│   ├── app/                 Entrypoint, MainWindow, app-level glue
│   ├── core/                Workspace, undo, journal, ID types
│   ├── cues/                Cue base, all cue subclasses
│   ├── show/                Show file I/O (SQLite + JSON sidecar)
│   ├── audio/               AudioEngine, matrix mixer, decoders
│   ├── video/               VideoEngine, FFmpeg pipeline, Qt RHI render
│   ├── lighting/            DMX-USB, Art-Net, sACN, lighting state model
│   ├── osc/                 Hand-rolled OSC 1.1 codec + transports
│   ├── midi/                RtMidi wrapper, MSC framing
│   └── ui/                  Inspector panels, cue-list view, GO bar, theme
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_smoke.cpp       Configures-and-builds smoke check
│   └── (per-module tests beside the modules they exercise)
│
├── docs/
│   ├── osc-coverage.md      OSC 1.0/1.1 feature matrix (the source of truth)
│   ├── cue-types.md         Per-cue-type spec
│   ├── keyboard-shortcuts.md
│   └── performance-budgets.md
│
├── resources/
│   ├── icons/               App icon, cue type glyphs
│   └── themes/              QSS themes (dark/light/high-contrast)
│
└── .github/
    └── workflows/
        └── ci.yml           Win/Mac/Linux CMake build matrix
```

Each `src/<module>/` has its own `CMakeLists.txt` building a static library; `app/` links them all into the final executable.

---

## 2. Build system

- **CMake ≥ 3.24**, presets-based.
- Qt 6.11+ found via `find_package(Qt6 6.11 …)` against the system install. **No vcpkg.** This keeps the build simple and avoids tooling that obscures what's pulling in what.
- Other deps fetched on demand via `FetchContent` with pinned commit SHAs:
  - **FFmpeg** — preferred via system package (`libavformat-dev` on Linux, brew on Mac, prebuilt zip on Windows). FetchContent fallback only for Windows CI.
  - **RtMidi** — single-file, vendored under `third_party/rtmidi/` to avoid the FetchContent step entirely.
- Presets: `windows-debug`, `windows-release`, `macos-debug`, `macos-release`, `linux-debug`, `linux-release`. Each pins generator, build dir, and toolchain.
- C++23, warnings-as-errors in CI, `-Wall -Wextra -Wpedantic` (or `/W4` MSVC).

---

## 3. Dependency policy

The user's directive: **minimize external dependencies.** Every dep we adopt costs us tracking, updating, supply-chain risk, and lost legibility.

Decision rule: if the protocol/feature can be implemented in <500 LOC by a competent C++ programmer reading the spec, write it ourselves. Otherwise consider a dep.

| Concern | Decision | Rationale |
|---|---|---|
| GUI framework | Qt 6 | Required; nothing else gives us native cross-platform GUI + RHI + multimedia + sql + network in one package |
| OSC 1.0/1.1 codec | **In-tree** | We need full 1.1 coverage; no library does it cleanly; ~1000 LOC total |
| OSC transports (UDP/TCP/WS) | **In-tree** over Qt sockets | Qt already gives us QUdpSocket/QTcpSocket/QWebSocket |
| Art-Net output | **In-tree** | Single 18-byte header + DMX payload; ~200 LOC |
| sACN output | **In-tree** | E1.31 is well-specified; ~300 LOC |
| MSC framing | **In-tree** | MIDI sysex framing of standard MSC commands; ~200 LOC |
| DMX-USB output | **In-tree** native serial | Win32 SetupComm + WriteFile, termios on Mac/Linux; avoids libftdi |
| MIDI device I/O | **RtMidi** (vendored) | Cross-platform MIDI device enumeration + I/O is non-trivial; RtMidi is single-file MIT |
| Audio device I/O | **Qt Multimedia** initially | Already in Qt; if Phase-3 latency benchmark fails on ASIO, drop in **miniaudio** (single-header MIT/0BSD) |
| Audio matrix mixer | **In-tree** | A NxM gain matrix with smoothing is a screenful of code |
| Audio decode | Via Qt Multimedia / FFmpeg | Through the same FFmpeg dep we already need for video |
| Video decode | **FFmpeg** (LGPL build) | Reimplementing codecs is unrealistic |
| Video render | **Qt RHI** | Already in Qt; abstracts Vulkan/Metal/D3D12 |
| Spout/Syphon/NDI | Optional, runtime-loaded | Only loaded if the user enables external video sharing |
| Logging | **Qt's QLoggingCategory** | Already in Qt; structured, fast, filterable |
| Testing | **Qt Test** | Already in Qt; no Catch2/GoogleTest |
| Scripting (Phase 7+) | **Lua + sol2** | Header-only, well-trodden, optional dep |

**Total runtime non-Qt deps in MVP: FFmpeg + RtMidi.** Ship binary fits in 30 MB.

---

## 4. Module dependency graph

```
                        ┌─────┐
                        │ app │
                        └──┬──┘
                           │
              ┌────────────┼────────────────────┐
              ▼            ▼                    ▼
           ┌─────┐     ┌──────┐             ┌──────┐
           │ ui  │ ───▶│ core │ ◀────── ─── │ show │
           └──┬──┘     └──┬───┘             └──┬───┘
              │           │                    │
              ▼           ▼                    │
           ┌─────┐    ┌──────┐                 │
           │cues │───▶│ core │                 │
           └──┬──┘    └──────┘                 │
              │                                │
        ┌─────┼──────┬──────┬──────┬──────┐    │
        ▼     ▼      ▼      ▼      ▼      ▼    │
     ┌─────┬─────┬──────┬──────┬─────┐         │
     │audio│video│light │ osc  │midi │ ◀───────┘
     └─────┴─────┴──────┴──────┴─────┘
```

- `core` is the dependency sink; nothing it depends on except `Qt::Core`.
- Output subsystems (`audio`, `video`, `lighting`, `osc`, `midi`) are siblings — they don't talk to each other directly. The `cues` layer composes them.
- `ui` only knows about `core`, `cues`, and `show`. It dispatches to engines via signals/slots through cue invocation, never by reaching into the engines.
- `app` is the only module that constructs and owns engine singletons.

---

## 5. Threading model

| Thread | Owner | Allowed to block? | Real-time? |
|---|---|---|---|
| **UI** | Qt main loop | Briefly (file I/O, dialogs) | No |
| **Show I/O** | `show::ShowFile` | Yes (disk reads) | No |
| **Engine scheduler** | `core::GoEngine` | No | Soft RT |
| **Audio device callback** | OS-driven | **NO mutexes, NO allocations, NO disk** | **Hard RT** |
| **Audio file decode** | Per-cue task | Yes (disk + FFmpeg) | No |
| **Video decode** | Per-cue task | Yes | No |
| **Video render** | RHI-owned | No | Soft RT (60 fps) |
| **OSC transport** | `osc::OscEngine` | No on hot send path | Soft RT |
| **MIDI I/O** | `midi::MidiEngine` | No | Soft RT |
| **Lighting tick** | `lighting::LightingEngine` | No (44 Hz tick) | Soft RT |

Inter-thread communication: **lock-free SPSC ring buffers** for the audio path, Qt signals/slots (queued connections) for everything else.

---

## 6. Real-time guarantees

The audio device callback is the only hard-real-time thread. Rules in `audio/`:

- No `new`/`delete`, no `std::vector::push_back`, no Qt API calls.
- All buffers are pre-allocated when a cue is loaded, recycled on stop.
- Communication with the rest of the app is exclusively through a lock-free SPSC ring of fixed-size command structs (`audio::Command`).
- Sample-rate conversion uses a fixed-size cubic interpolator; no dynamic allocation.
- Fades are computed inline in the callback with pre-baked envelope tables.

OSC and MIDI sends are soft-real-time: serialized off-thread, written to the wire from the engine thread, never from UI.

---

## 7. Document model

```
Workspace
  ├── CueLists  (one per tab; 1+)
  │     └── Cues (ordered)
  │           └── Cue children (groups)
  │
  ├── Patches
  │     ├── AudioOutput[]
  │     ├── DmxUniverse[]
  │     ├── OscDestination[]
  │     ├── MidiPort[]
  │     └── VideoSurface[]
  │
  └── Settings (preferences scoped to this show)
```

- `Cue` is a polymorphic base. Subclasses register at startup via `cues::CueRegistry`. Adding a cue type = one source file + one register call.
- All editable fields are wrapped in `core::Property<T>` to integrate with the undo stack and the property-binding system.
- Cue references are by stable `CueId` (UUID), never by index — so reordering doesn't break cross-references in fade/start/stop targets.

---

## 8. Persistence format

**Primary:** SQLite database in a single file with extension `.quewi`.

Schema (sketch):

```sql
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE cue_lists (id BLOB PRIMARY KEY, name TEXT, ord INTEGER);
CREATE TABLE cues (
  id BLOB PRIMARY KEY,
  list_id BLOB REFERENCES cue_lists(id),
  parent_id BLOB,             -- for group children
  ord INTEGER,
  type TEXT,                  -- 'audio', 'osc', 'fade', ...
  number REAL,
  name TEXT,
  pre_wait REAL,
  post_wait REAL,
  continue_mode INTEGER,
  notes TEXT,
  payload BLOB                -- JSON, type-specific
);
CREATE TABLE patches_audio_output (...);
CREATE TABLE patches_dmx_universe (...);
CREATE TABLE patches_osc_dest (...);
CREATE TABLE patches_midi_port (...);
CREATE TABLE patches_video_surface (...);
CREATE TABLE assets (id BLOB PRIMARY KEY, path TEXT, sha256 TEXT, size INTEGER);
```

- Type-specific cue data lives in the `payload` JSON blob. Schema-versioned via `meta`.
- Foreign keys enforced.
- WAL mode for safe concurrent reads while playing.
- All large assets (audio/video files) are referenced by path, not embedded.

**Sidecar JSON manifest:** on every save, a `myshow.quewi.json` is written next to the `.quewi` file with a deterministic JSON dump of cues, patches, and metadata. Asset blobs are excluded. This gives users diffable git-friendly tracking.

---

## 9. Plugin/extension points (post-1.0)

- **Cue type registry** — `CueRegistry::register("my-cue", factory)` adds a new cue type at startup.
- **Output adapter registry** — `OutputRegistry::register("my-output", factory)` adds e.g. a new lighting transport.
- **Asset importer registry** — file-extension → cue-creation hook.

In Phase 0–7, these registries exist but only built-in types use them.

---

## 10. Testing strategy

- **Unit tests (Qt Test):** every non-trivial codec/parser. OSC parser is fuzz-tested with `libFuzzer` on Linux CI.
- **Integration tests:** spin up an OSC server in-process, fire an OSC cue, assert bytes on the wire.
- **Soak tests:** play a 200-cue show through, check for memory leaks (Valgrind on Linux CI).
- **UI smoke tests:** Qt Test's GUI mode runs scripted user flows headless on CI.
- **Performance gates:** the cold-start and idle-RAM budgets from `design.md §2` are asserted in CI on a fixed reference machine. Failure blocks merge.

---

## 11. CI/CD

- **GitHub Actions** matrix: `windows-latest`, `macos-latest`, `ubuntu-latest`.
- Qt installed via `jurplel/install-qt-action` (wraps `aqtinstall`).
- Each platform: configure → build → run unit tests → run smoke test (launch app headless, assert window opens, exit).
- Release tags trigger packaged builds: NSIS installer (Win), notarized `.dmg` (Mac, requires signing secrets), AppImage (Linux). Code signing is post-1.0.

---

## 12. Coding standards

- C++23.
- `clang-format` (LLVM base, 4-space indent, 100-col).
- `clang-tidy` checks: bugprone-*, performance-*, modernize-*, readability-* (curated subset).
- Naming: `PascalCase` for types, `camelCase` for functions/members, `m_` prefix only where Qt convention requires (override), `k` prefix for compile-time constants.
- Headers self-contained; no transitive include reliance.
- `#pragma once` over include guards.
- No exceptions across module boundaries; engines use `std::expected`-style returns (`core::Result<T>`).

---

## 13. Logging

- Qt's `QLoggingCategory` per module: `quewi.osc`, `quewi.audio`, etc.
- Default: `*.info=true *.debug=false`. Users override via `QT_LOGGING_RULES` env var or Preferences.
- Log file at `<configdir>/quewi/log.txt`, rotated daily, 10-file retention.
- A "Capture diagnostics" menu action zips the last 24h of logs + the current show file (with assets stripped) into a bug report bundle.

---

## 14. Versioning & release process

- Semver: `MAJOR.MINOR.PATCH`.
- File-format compatibility: minor versions read each other's files; major bumps require a one-shot in-app migration.
- Release cadence: monthly minor during MVP, at-will after.
- Tag → CI builds installers → maintainer publishes GitHub Release with notes auto-generated from PR titles.
