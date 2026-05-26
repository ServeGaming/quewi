# Credits

Quewi is built on a small handful of excellent open-source
projects. None of this would be possible without them.

---

## Direct dependencies

**[Qt 6](https://www.qt.io/)** — GUI, networking, audio I/O,
PDF rendering, WebSockets, SQL. Licensed under LGPL-3.0.
Quewi targets Qt 6.7+ as the minimum.

**[FFmpeg](https://ffmpeg.org/)** (LGPL build) — Video / audio
codec coverage. Ships dynamically linked so users can swap in
their own builds if needed.

**[RtMidi](https://www.music.mcgill.ca/~gary/rtmidi/)** by Gary
P. Scavone — Cross-platform MIDI device I/O. The only library
for cross-platform MIDI I/O that didn't make us cry.

---

## Inspiration

**[QLab](https://qlab.app/)** by Figure 53 — the gold-standard
macOS-only theatre cueing app. Quewi's UX takes a lot from
QLab's structure: cue lists, the GO concept, control cues, the
inspector pattern. We aim for feature parity over time.

**[Stagehand](https://stagehand.app/)** — also macOS-only, has
some good UX ideas for projection mapping.

**[SCS (Show Cue Systems)](https://www.showcuesystems.com/)** —
Windows-first commercial app. Influence on the cue list /
keyboard-first workflow.

---

## Protocols implemented

- **OSC 1.1** — hand-written codec, full type-tag coverage,
  pattern matching, bundles, multiple transports
- **sACN (E1.31)** — DMX over Ethernet, multicast & unicast,
  44 Hz refresh
- **Art-Net** — alternate DMX-over-Ethernet
- **DMX-USB** — Enttec Open/Pro serial framing
- **MIDI Show Control (MSC)** — hand-written SysEx encode/decode

---

## Distribution / tooling

- **CMake** — build system
- **CPack** with WIX (Windows), DragNDrop (macOS), AppImage tools
  (Linux) — installer packaging
- **GitHub Actions** — CI / release pipeline
- **MkDocs Material** — this documentation site

---

## License compatibility

| Component | Their license | Compatible with AGPL-3.0? |
|---|---|---|
| Qt 6 | LGPL-3.0 | Yes (dynamic linking) |
| FFmpeg (LGPL) | LGPL-2.1+ | Yes |
| RtMidi | MIT-modified | Yes |

---

## The team

Quewi is currently maintained by **ServeGaming** with substantial
AI-pair-programming assistance. The git log credits both human
and AI contributions where each landed material work.

Theatre-specific consultation and field-testing happen as the
project is used in real productions. If you've used quewi for a
show, drop a note via the [issue tracker](https://github.com/ServeGaming/quewi/issues)
— "this worked for our run of X" feedback is gold.

---

## Acknowledgements

Special thanks to:

- Anyone who's run quewi in a tech rehearsal and reported a bug
  rather than just live-tweeting the crash
- The QLab team for proving that this category of software can
  be excellent
- The theatre operator community for being patient with v0.9.x
  rough edges while we get to 1.0
