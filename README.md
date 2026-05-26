# quewi

Theatre cueing software. Sound, light, projection. Every OSC feature in the spec. A UI that doesn't fight you.

**Documentation: [servegaming.github.io/quewi](https://servegaming.github.io/quewi/)** — install instructions, quickstart, cue-type reference, OSC API.

## Download

Pre-built installers for Windows, macOS, and Linux are attached to every [GitHub Release](https://github.com/ServeGaming/quewi/releases).

| Platform | Artifact | Notes |
|---|---|---|
| Windows | `quewi-X.Y.Z-win64.msi` | Standard installer to Program Files |
| Windows | `quewi-X.Y.Z-win64-portable.zip` | Portable — extract anywhere, supports one-click in-app updates |
| macOS   | `quewi-X.Y.Z-macos.dmg` | Drag-to-Applications; includes a Fix Gatekeeper recovery script |
| Linux   | `quewi-X.Y.Z-linux-x86_64.AppImage` | Run anywhere; in-place updates |

See [docs → Install](https://servegaming.github.io/quewi/getting-started/install/) for per-platform details and Gatekeeper / SmartScreen guidance.

## What it is

quewi is a cross-platform theatre cueing application in the spirit of QLab — a single GO button drives an ordered list of cues that fire sound, video, lighting, OSC, and MIDI in lock-step.

Design goals:

- **QLab-parity feature set** — audio, video, image, light, fade, group, OSC, MIDI, MSC, network, timecode, script cues.
- **Stupid-proof UI** — one screen, three panes, a big GO button, color-coded states, no hidden modes, undo everywhere, a pre-flight check before show.
- **Full OSC 1.1 coverage** — every type tag, every transport (UDP, TCP/SLIP, WebSocket), pattern matching, time tags, OSC Query. Hand-written so we own the bytes.
- **Lightweight & fast** — sub-500 ms cold start, <150 MB RAM with a 200-cue show, GO-to-output under 5 ms median, smooth on a 165 Hz monitor.
- **Few dependencies** — Qt 6, FFmpeg, RtMidi. Everything else (OSC, Art-Net, sACN, MSC, DMX-USB, audio matrix mixer, EQ/comp/reverb/delay) is in-tree, hand-written, and small.

## What works in 0.1.0

- Cue list with multi-list tabs, drag-to-reorder, batch delete, group disclosure scaffolding, color-tinted rows, type icons, live state column.
- Inspector with per-cue-type forms, undo/redo, command palette (Cmd/Ctrl+K), shortcuts editor, find/replace.
- Audio engine: multi-device output, per-voice gain/pan/fade, live peak meters, sample-rate conversion, full audio editor (multi-track timeline, regions, trims, fades, 6-band parametric EQ with five filter types, compressor, reverb, delay, FFT spectrogram, render to 24-bit WAV).
- OSC engine: UDP, TCP/SLIP, WebSocket transports; full 1.0 + 1.1 type tags; pattern matcher; live monitor; OSC Query HTTP server; importable/exportable namespace dictionary.
- MIDI: device enumeration via RtMidi, basic MIDI cue.
- Lighting: skeleton engine + cue type. Full DMX/Art-Net/sACN output is on the roadmap.
- Video: cue type and player skeleton. Phase 5 in progress.
- Show file format: SQLite-backed `.quewi` with atomic writes and a crash-recovery journal.
- Show Mode lock for operators.
- Auto-save journaling with on-startup recovery.

## Building from source

**Prerequisites**

- CMake ≥ 3.24
- A C++23 compiler (MSVC 2022 17.6+, Apple Clang 16+, or GCC 13+)
- Qt 6.11+ with `Widgets`, `Network`, `Sql`, `Multimedia`, `MultimediaWidgets`, `WebSockets`, and `Test`.
- Ninja (recommended)

**Build**

```bash
cmake --preset windows-release      # or macos-release / linux-release
cmake --build --preset windows-release
```

The Release preset is what you want for actual use — Debug Qt is dramatically slower than Release and the difference is visible on a high-refresh monitor.

The executable lands in `build/<preset>/src/app/quewi.exe` (or the platform equivalent).

**Tests**

```bash
ctest --preset windows-debug --output-on-failure
```

**Packaging an installer (Windows, MSI)**

```bash
cd build/windows-release
cpack -G WIX
```

The MSI lands in the same directory. Requires WiX Toolset 3.x on `PATH`.

## Performance

Targets, asserted in CI where feasible:

| Budget | Target |
|---|---|
| Cold start to usable UI | < 500 ms |
| Idle CPU (show loaded, nothing running) | < 0.5 % of one core |
| Idle RAM (200-cue show) | < 150 MB |
| GO → first sample / first OSC packet | < 5 ms median, < 15 ms p99 |
| Cue-list scroll/select at 10 000 cues | 60 fps (virtualised) |
| Main executable size | < 30 MB (excl. shared libs) |

## Security

See [`SECURITY.md`](SECURITY.md) for the threat model, audit findings, and mitigations. tl;dr: quewi is a desktop app, not a server; OSC listeners bind to all interfaces by default (operator-expected on a stage network), so don't run it on a public LAN. Treat `.quewi` files like office macros — only open ones from people you trust.

To report a vulnerability, open a private GitHub Security Advisory.

## License

**AGPL-3.0** — see [`LICENSE`](LICENSE).

In plain English: you can use, study, modify, and redistribute quewi freely. If you distribute a modified version — including running it as a network service that users interact with — you must publish your full source code under the same license. The AGPL is the strongest copyleft in the GPL family; it explicitly closes the "SaaS loophole" that lets companies fork open-source projects and run them as proprietary services.

Nobody — including the original author — can ship a closed-source paid fork.

## Contributing

Issues and PRs welcome. Read [`UX.md`](UX.md) and [`structure.md`](structure.md) for the architectural picture, and [`SECURITY.md`](SECURITY.md) for the project's security posture.
