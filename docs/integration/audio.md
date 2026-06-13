# Audio routing

Quewi plays audio through whatever the OS exposes as an output
device. Quality and latency depend on the device — built-in is
fine for testing, a real interface (RME, Focusrite, MOTU, etc.)
is what you want for shows.

---

## Output devices

**Preferences → Audio → Output device** lists every device the OS
sees. Picking one routes every audio cue (without an explicit
per-cue device) through it.

Per-cue override: in the Inspector for an Audio cue, set the
**Output device** field. That cue plays through the specified
device regardless of the global default. Useful for sending one
cue to a backstage cue speaker while everything else goes to FOH.

---

## Channel routing — the output matrix

Multi-output devices expose multiple physical channels (FOH L, FOH
R, Center, Sub, Lobby L, Lobby R, …). The Audio cue's **output
matrix** sends the stereo audio of the cue to any combination of
those outputs at independent gains.

| Output | Channel | Gain (dB) |
|---|---|---|
| FOH L | 1 | 0 |
| FOH R | 2 | 0 |
| Center | 3 | -∞ (silenced) |
| Sub | 4 | -6 |
| Lobby L | 5 | -12 |
| Lobby R | 6 | -12 |

The matrix applies **after** the cue's gain and pan. A `pan` of
+1 sends to right; the matrix then takes the post-pan stereo
signal and splatters it across the configured outputs.

---

## Object audio (spatial)

For cues with **Object Audio** enabled, quewi uses VBAP
(Vector-Base Amplitude Panning) to position the source in 3D
space and render it across the configured speaker array.

Inputs:

- **Speaker patch** — defines speaker positions (azimuth +
  elevation) in your venue
- **Azimuth** (-180 … 180°) — where the source is horizontally
- **Elevation** (-90 … 90°) — where the source is vertically
- **Spread** (0 … 1) — point source (0) to omnidirectional (1)

A **trajectory** can animate azimuth/elevation/spread over time,
sampled at 30 Hz — useful for "voice walks across the stage"
effects.

Object audio bypasses the output matrix (the matrix is for
stereo cues). The same audio cue can have an output matrix OR
object-audio routing; the inspector enforces this.

---

## Buffer sizes / latency

Preferences → Audio → **Buffer size** (samples).

- **Smaller** (256, 128) — lower latency, more glitchy on heavy
  load.
- **Larger** (1024, 2048) — higher latency, more headroom.

For most theatre use, **1024 samples at 48 kHz ≈ 21 ms** is the
sweet spot. Inaudible to the audience, comfortable for the
audio thread.

For live monitoring through quewi (singer hears themselves), go
as low as your hardware allows. RME interfaces routinely hit
128 samples (≈ 2.7 ms) on macOS.

---

## Pre-decoding

When you open a show, quewi walks every Audio cue and
pre-decodes the first chunk of each file. This means GO triggers
playback from an already-warm buffer, sub-5-ms typical.

The Preferences → Audio → **Pre-decode memory budget** caps how
much pre-decoded audio quewi keeps in RAM at once. Default 512
MB. The status bar shows current vs. budget; if you're near the
ceiling, increase it or pre-decode less aggressively.

---

## Effects rack

Per-cue effects chain — EQ, Compressor, Reverb, Delay. See the
[Audio cue page](../cue-types/audio.md) for the per-effect
parameter reference. Effects apply during live playback AND
during the editor's render-to-WAV export.

Both the **EQ** and the **Compressor** have a visual editor behind
the **Edit…** button on their rack row:

- **Parametric EQ** — a response graph with one draggable handle
  per band.
- **Compressor** — an interactive transfer curve (input level →
  output level). Drag the threshold handle horizontally, drag the
  right-edge ratio handle vertically, and roll the wheel to widen
  the soft knee. A gain-reduction meter down the right edge moves
  while you audition the cue so you can see the compressor working.
  Double-click resets threshold / ratio / knee / makeup to defaults.

---

## Audio editor

Open from the Inspector for an Audio cue: **Open in Audio
Editor…** for the dedicated waveform / region / fade / spectro
view. Edit regions non-destructively (the source file is
untouched), then save back to the cue.

### Waveform vs. Spectrogram view

The toolbar's **View** toggle switches every track between the peak
**Waveform** and an Audacity-style **Spectrogram** — a log-frequency
heat-map of the entire clip drawn inline in the timeline. The
spectrogram is computed once per source file on a background thread
and cached, so toggling and scrolling stay responsive; it scales as
you zoom. (A focused per-selection spectrogram also lives in the
bottom panel's **Spectrogram** tab.)
