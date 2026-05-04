# quewi documentation

quewi is a cross-platform theatre cueing app. This documentation tree
covers what's actually built today — the README points back here, and
each chapter is self-contained for copy-paste into wikis or to feed an
LLM with focused context.

## For users

- [**OSC remote API**](osc-remote-api.md) — every OSC address quewi
  listens for. Use this when you're building a remote (iPad app,
  Stream Deck, Companion module) that drives quewi over the network.
- [**Audio cues**](audio.md) — file formats, the gain/fade/trim/pan
  model, and how the in-cue editor works today (and how it will grow
  toward the planned full audio editor).
- [**Lighting cues**](lighting.md) — sACN / E1.31 output, universe
  numbering, channel editor.
- [**Video cues**](video.md) — Video / Image / Text cue behaviour and
  output-window placement on multi-monitor rigs.
- [**Cue types reference**](cue-types.md) — every cue type's parameters
  and "what fires when GO is pressed."
- [**Keyboard shortcuts**](keyboard-shortcuts.md) — full table.

## For developers

- [**OSC coverage matrix**](osc-coverage.md) — what's implemented in
  the OSC layer (separate from the remote API the user-facing remote
  speaks).
- [**Performance budgets**](performance-budgets.md).
- [**Architecture**](../structure.md).
- [**UX vision**](../UX.md).
- [**Visual design tokens**](../DESIGN.md).
- [**QLab parity roadmap**](qlab-parity-roadmap.md).

## Status snapshot

quewi is in active development. As of the latest commit:

- ✅ Cue types: Memo, OSC, Audio, Fade (audio target), Light, Light Fade,
  Video, Image, Text — nine total
- ✅ Engines: OSC (full 1.1 codec, UDP/TCP-SLIP/WebSocket), Audio
  (multi-voice mixer with gain/fade/trim/pan), Lighting (sACN multicast
  with per-channel fades), Video (Qt Multimedia output windows)
- ✅ Drag-and-drop file → cue auto-detect
- ✅ Cue colors with whole-row tinting plus a state dot column
- ✅ Inline audio editing: trim, pan, normalize, reverse
- ✅ OSC remote API for iPad / Stream Deck / Companion
- ⬜ Full audio editor (Audacity-like multi-track) — Phase 9
- ⬜ MIDI / MSC — Phase 6
- ⬜ Group cues with all five modes — Phase 6
- ⬜ Pre-flight, Show Mode lock, command palette — Phase 7
- ⬜ Installers + signing — Phase 8

For the complete checklist of remaining work, see
[the QLab-parity roadmap](qlab-parity-roadmap.md).
