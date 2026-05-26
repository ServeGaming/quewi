---
hide:
  - navigation
  - toc
---

# quewi

**Theatre cueing software.** Open-source, cross-platform, OSC-controllable.

Quewi is a desktop cue-show runner for theatre, live events, and any
context where someone needs to press GO and have audio fire, lights
shift, video play, OSC ship to a console, and MIDI Show Control go
out to a hardware desk — in any combination, in the right order, at
the right time.

It runs on Windows, macOS, and Linux from the same codebase. It
loads sound, video, and lighting cues in the same list. It exposes
every important action over OSC so you can drive it from a tablet,
a Stream Deck, or a custom controller — and the OSC API is fully
bidirectional, with live push notifications, not just one-way
triggers.

---

## Get started

<div class="grid cards" markdown>

-   :material-download:{ .lg } **Install**

    ---

    Download quewi for Windows, macOS, or Linux. One installer per
    platform.

    [Install →](getting-started/install.md)

-   :material-rocket-launch:{ .lg } **Quickstart**

    ---

    Build a five-cue show from scratch in five minutes. Audio,
    OSC, fade, GO.

    [Quickstart →](getting-started/quickstart.md)

-   :material-school:{ .lg } **Concepts**

    ---

    What's a cue. What's a cue list. What does GO actually do.
    Read this once before you build a show for real.

    [Concepts →](getting-started/concepts.md)

-   :material-lan-connect:{ .lg } **OSC control**

    ---

    Drive quewi from another app over the network. Mirror state,
    fire cues, edit fields, subscribe to live updates.

    [OSC reference →](osc-control/reference.md)

</div>

---

## What it does

- **Cue lists** — flat or nested, multiple lists per workspace, drag
  to reorder, undo everywhere. Show files are SQLite + JSON, plain
  files you can version-control.
- **Audio playback** — multi-channel, fade-in/out, trim, loop, per-output
  matrix routing, an effects rack (EQ / Compressor / Reverb / Delay),
  object-audio placement with VBAP. Scrubbable progress while playing.
- **Lighting** — sACN multicast, Art-Net, DMX-USB. Light cues set
  channel values; Light Fade cues animate between snapshots.
- **Video** — Qt RHI compositor, multi-output windows, geometry +
  opacity + corner-pin, image + text overlays, file playback via
  FFmpeg.
- **OSC dispatch and remote control** — full OSC 1.1: UDP, TCP/SLIP,
  WebSocket; bundles; pattern matching; type tags. Quewi can both
  send OSC (as cues) and be controlled by OSC (every menu action
  exposed at `/quewi/...`).
- **MIDI + MSC** — RtMidi for device I/O, hand-written MIDI Show
  Control codec for talking to consoles that speak that protocol.
- **Show Mode** — locks the UI down to GO / Panic / Fade-All. PIN
  to unlock so an accidental Ctrl-Z mid-show is impossible.
- **Pre-flight** — validates every file exists, every OSC destination
  resolves, every DMX universe is patched, before the operator
  starts the show.
- **In-app updates** — Linux and macOS swap-in-place on update; no
  installer dialogs.

---

## What it costs

It's [AGPL-3.0](about/license.md). Free to use, source available,
modify and distribute as long as you keep the same license. Nobody
— including the original author — can charge money for it.

---

## What's next

- Read the [quickstart](getting-started/quickstart.md) to build your
  first show.
- Skim the [concepts](getting-started/concepts.md) page so the
  terminology lines up before you go further.
- Check the [OSC remote API](osc-control/reference.md) if you're
  building an external controller.
- Hit a problem? Start with [troubleshooting](support/troubleshooting.md)
  or [open an issue](https://github.com/ServeGaming/quewi/issues).
