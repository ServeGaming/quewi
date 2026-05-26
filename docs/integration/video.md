# Video output

Quewi composites video, images, and text on a per-output basis
via Qt RHI (Rendering Hardware Interface — the abstraction over
OpenGL / Metal / D3D). One output window per attached display
you've configured to receive cues.

---

## Configuring outputs

**Tools → Projection Mapping** opens the output editor.

For each attached display:

- **Enable** as a quewi output
- **Geometry** — full screen or windowed region
- **Corner pin** — four-point projection mapping for tilted /
  trapezoidal projection surfaces

When enabled, quewi opens a borderless window on that display
and uses it as the canvas for any cue targeting that screen
index.

---

## Surface model

Every visual cue ([Video](../cue-types/video.md),
[Image](../cue-types/video.md), [Text](../cue-types/video.md))
has a surface — a rectangle within the target screen:

| Field | Meaning | Range |
|---|---|---|
| `screenIndex` | Which output | 0 … N-1 |
| `posX` | Left edge | 0 … 1 (normalised) |
| `posY` | Top edge | 0 … 1 |
| `posW` | Width | 0 … 1 |
| `posH` | Height | 0 … 1 |
| `opacity` | Alpha | 0 … 1 |

Normalised coordinates mean a surface that's full-screen on a
1080p display is also full-screen on a 4K display — geometry is
relative to the output, not pixel-absolute.

Multiple visual cues can fire simultaneously; they composite via
the compositor in fire order (later cues paint over earlier
ones, like Photoshop layers).

---

## File support

Video cues use Qt Multimedia → FFmpeg, so the codec coverage
matches FFmpeg's:

- **Video**: H.264, H.265, ProRes, VP9, AV1, DNxHD/HR — anything
  FFmpeg can decode.
- **Audio in video**: extracted and routed to the video output's
  associated audio bus by default.
- **Container**: MP4, MOV, MKV, WebM, MXF, …

For best playback performance: **ProRes 422 Proxy** at the show's
native resolution. Less CPU than H.264 for the same image
quality, no compression artifacts on long fades.

---

## Latency

Video cues have higher fire latency than audio — decoding the
first frame + RHI texture upload takes 50-200 ms depending on
codec. For zero-frame-drop precision, **trigger a Video cue
slightly before** the corresponding moment in the show.

A pre-warm mode (decode the first frame at workspace load) is
on the roadmap.

---

## Stop / blackout

`/quewi/video/stop` (OSC) or pressing Panic (<kbd>Esc</kbd>)
stops every video voice and clears every output to black.

Individual cues can be stopped via the ACTIVE strip's Stop
button or by firing a [Stop cue](../cue-types/control-cues.md)
that targets the running video cue.

---

## External outputs (Spout / Syphon / NDI)

Not in MVP. Planned post-1.0:

- **Spout** (Windows) — share a video texture with other
  applications (Resolume, OBS, etc.) without a capture card.
- **Syphon** (macOS) — same idea, macOS-native.
- **NDI** — network-distributed video.

These would let quewi run as the cue engine while a dedicated
media server handles the heavy compositing.
