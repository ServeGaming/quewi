# Video / projection in quewi

quewi's visual output uses Qt Multimedia's `QMediaPlayer` for video
decoding and standard QWidgets for image and text. Each visual cue
opens its own frameless top-level output window when fired, sized
within the chosen screen's geometry.

No separate FFmpeg dependency is required — Qt wraps the platform's
native decoder (Media Foundation on Windows, AVFoundation on macOS,
gstreamer on Linux).

## Cue types

### Video cue (`video`)

Plays a video file. Supports any codec your platform's media stack
decodes — typically H.264/H.265 in MP4/MOV containers, plus
VP8/VP9/AV1 in WebM and Apple ProRes on macOS.

Fields:

| Field | Range | Notes |
|---|---|---|
| File path | path | Absolute |
| Loop | bool | Loops at end of file |
| Screen | 0..n | Display index (0 = primary) |
| Position x, y | 0..1 | Normalised to chosen screen |
| Size w, h | 0..1 | Normalised to chosen screen |
| Opacity | 0..1 | Window opacity |

### Image cue (`image`)

Static image (PNG, JPG, GIF, BMP, TIFF, WebP). Same screen / position
/ size / opacity controls as Video.

### Text cue (`text`)

Renders a string with a chosen font size and colour over a
black-or-transparent background. Useful for titles, lyrics, sign-language
text, captions, surtitles.

Extra fields beyond the visual common set:

| Field | Notes |
|---|---|
| Text | The string to display |
| Text size | Pixel size of the rendered font |
| Text colour | Picker including alpha |

## Multi-monitor setup

quewi reads `QGuiApplication::screens()` once on app launch. The list
is in the order Qt enumerates physical displays — this matches what
Windows / macOS / X11 report in their respective display arrangements.

To verify which screen is which, fire a Text cue with the screen's
index as its content (e.g. text = "0", screen = 0; another cue text =
"1", screen = 1) and watch where they appear.

## What this build does NOT do yet

These come later:

- Multiple cues compositing on a shared "surface" — today each cue is
  its own top-level window. Compositing arrives with the GoEngine
  in Phase 6.
- **Spout / Syphon / NDI** — runtime-loaded video sharing planned but
  not wired.
- **Edge blending / corner pin / mesh warp** — projection mapping
  features for dome / curved-surface rigs. Big rabbit hole; deferred
  past 1.0.
- **Audio routing** for video files. The cue's own audio plays through
  the system default; the audio engine's matrix mixer doesn't see it
  yet. Phase 6 wires this up.
- **Geometry fade** — the FadeCue parameter set will grow to include
  `posX/posY/posW/posH/opacity` when video lands fully. Until then,
  fades only target audio cue gain.

## Performance

Decoding happens on Qt Multimedia's worker thread. The output window
is a `QVideoWidget` rendering through Qt RHI — uses the platform GPU.

Tested baseline (Windows 11 + RTX 3060):

- 1080p H.264 → smooth at native rate
- 4K H.264 → smooth
- 4K HEVC → smooth on hardware decode; soft if the decoder falls back
  to CPU

If a video stutters: confirm the file's codec is hardware-accelerated
on your GPU. `ffprobe input.mp4` shows the codec; running it through
HandBrake to H.264 main-profile usually fixes legacy footage.
