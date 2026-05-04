# quewi

> Theatre cueing software. Sound, light, projection. Every OSC feature in the spec. A UI that doesn't fight you.

**Status:** pre-alpha — scaffolding only. See [`UX.md`](UX.md) and [`structure.md`](structure.md) for what we're building.

## What it is

quewi is a cross-platform theatre cueing application in the spirit of QLab — a single GO button drives an ordered list of cues that fire sound, video, lighting, OSC, and MIDI in lock-step. The goals:

- **QLab-parity feature set** — every cue type that matters: audio, video, image, light, fade, group, OSC, MIDI, MSC, network, timecode, script.
- **Stupid-proof UI** — one screen, three panes, big GO button, color-coded states, no hidden modes, undo everywhere, pre-flight check before show.
- **Full OSC 1.1 coverage** — every type tag, every transport (UDP/TCP/SLIP/WebSocket), pattern matching, time tags, OSC query. Hand-written so we own the bytes.
- **Lightweight & fast** — sub-500 ms cold start, <150 MB RAM with a 200-cue show, GO-to-output under 5 ms median.
- **Few dependencies** — Qt 6 + FFmpeg + RtMidi. Everything else (OSC, Art-Net, sACN, MSC, DMX-USB, audio matrix mixer) is in-tree, hand-written, and small.

## Building

> Currently the source tree is a skeleton — it configures and builds an empty Qt window. Real functionality lands in Phase 1+ (see [`UX.md`](UX.md) for the roadmap).

**Prerequisites:**
- CMake ≥ 3.24
- A C++23 compiler (MSVC 2022 17.6+, Apple Clang 16+, or GCC 13+)
- Qt 6.11+ (Widgets, Network, Sql, Multimedia, WebSockets, Test)

**Build:**

```bash
cmake --preset windows-debug    # or macos-debug / linux-debug
cmake --build --preset windows-debug
```

Run the produced executable in `build/<preset>/quewi.exe` (or platform equivalent).

## License

**AGPL-3.0** — see [`LICENSE`](LICENSE).

In plain English: you can use, study, modify, and redistribute quewi freely. If you distribute a modified version — including running it as a network service that users interact with — you must publish your full source code under the same license. The AGPL is the strongest copyleft in the GPL family; it explicitly closes the "SaaS loophole" that lets companies fork open source projects and run them as proprietary services.

This means: nobody — including the original author — can ship a closed-source paid fork.

## Contributing

Phase 0 just landed. Issues and PRs welcome once the architecture stabilizes (Phase 1). For now, read [`UX.md`](UX.md) and [`structure.md`](structure.md) and open issues for design feedback.
