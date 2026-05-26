# Audio in quewi

## Supported formats

quewi decodes audio through Qt's platform multimedia stack:

| Platform | Decoder | Reliable formats |
|---|---|---|
| Windows | Media Foundation | WAV, MP3, AAC/M4A, WMA |
| macOS | AVFoundation | WAV, MP3, AAC/M4A, AIFF, FLAC (limited) |
| Linux | gstreamer | Whatever gst plugins are installed |

If a file fails to decode, the inspector's audio panel shows the
specific error in red instead of the file metadata. The most common
cause on Windows is a missing codec; install Microsoft's standard
codec pack or transcode the file to WAV/MP3.

A native FFmpeg-backed decoder for full-format coverage is on the
Phase 9 list alongside the full audio editor.

## Per-cue parameters

Every Audio cue has these fields, all undoable and persisted:

| Field | Type | Description |
|---|---|---|
| File path | path | Absolute or workspace-relative |
| Gain | dB, -90 to +12 | Linear-domain gain applied at the mixer |
| Fade in | s, ≥ 0 | Linear ramp from 0 to gain over this duration |
| Fade out | s, ≥ 0 | Linear ramp to 0 when stop is requested |
| Trim in | s, ≥ 0 | Skip this many seconds at the start |
| Trim out | s, ≥ 0 | Stop playback at this offset (0 = play to end) |
| Pan | -1 … +1 | Equal-power stereo balance (only applied when output is stereo) |
| Loop | bool | Repeat from trim-in when reaching trim-out |
| Cue colour | colour or none | Tints the row in the cue list |

## Quick actions in the inspector

- **Normalize** — scans the decoded buffer for the absolute peak and
  scales every sample so the peak hits −1 dBFS. Non-destructive against
  the source file (re-pick the file to restore).
- **Reverse** — reverses the in-memory sample buffer frame-by-frame,
  preserving channel order. Same scope: in-memory only.

Both operations rebuild the waveform peak overview, so the visual
updates immediately.

## Engine architecture

The audio engine is documented in `structure.md §5`. Headlines:

- One `QAudioSink` opens the device lazily on the first `fire()` call.
  Idle quewi keeps the audio thread silent.
- Output format prefers 48 kHz stereo float32; falls back to the
  device's preferred format when needed.
- A custom `QIODevice` mixer pulls samples from active voices each
  callback, applies envelope + gain + pan, and sums into the output.
- Sample-rate conversion uses linear interpolation (per-voice).
  Band-limited resampling is on the polish list.
- Voice list is mutex-guarded for the hot path; sub-microsecond
  critical sections fit the < 5 ms GO-to-sample budget. A lock-free
  SPSC ring upgrade is scheduled for Phase 7 polish.

## Output device selection

Preferences → Audio lists every device returned by
`QMediaDevices::audioOutputs()`. Picking a different device hot-swaps
the engine — running cues stop with a click-suppression fade and the
new device becomes the output for any subsequent fire.

## Planned features (Phase 9 — the audio editor)

The single biggest feature still missing. The plan:

- Multi-track timeline editor on a dedicated window
- Sample-buffer editing with undo: cut, copy, paste, delete, silence,
  fade with curve picker (linear, equal-power, S-curve, custom)
- Effects rack per track: EQ, compression, reverb, delay, gain
  envelope
- Spectral view for surgical noise removal
- Export to wav/flac/mp3
- Bouncing edits to a derived "rendered" cue file so the live cue
  always plays a flat file (real-time effects come later)

The inline trim/pan/normalize/reverse you see today is the
non-destructive subset that can live entirely in the cue inspector.
The full editor gets its own modal window with its own undo stack.
