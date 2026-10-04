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
| In / Out | seconds | Where the cue starts and stops in the file (the video editor) |
| Picture fade in / out | seconds | The picture fades up from In and down to Out |

#### The video editor *(new in 1.0.4)*

Double-click a video cue (or press **Edit video…** in the Inspector, or
**Cue → Edit Video…**) to open it in the video editor. It's laid out like
Premiere Pro or DaVinci Resolve: one window with the viewer at the top,
settings on the right and the timeline along the bottom.

- **Viewer:** the frame at the playhead, the way the cue will show it, with
  its opacity and picture fades applied. Before the In point or after the
  Out point it's dimmed and labelled, because the cue never shows that part.
  Under it are the transport buttons (to In, frame back, play, frame on, to
  Out, loop), the timecode, and **Plays for**: how long the cue lasts with
  its trims and cuts.
- **Inspector** (right): collapsible sections with exact values.
  - **Clip:** In, Out (**End** means the end of the file), **Loop between
    In and Out**, Plays for, and the cuts.
  - **Video:** Opacity and the picture's Fade in and Fade out.
  - **Audio:** **Play the video's sound**, Level, Fade in and Fade out, and
    **Edit sound…** (the soundtrack in the audio editor, for effects and EQ).
  - **Lighting:** how many triggers the soundtrack has, and **Lighting
    triggers…**
- **Timeline:** **V1** is a strip of frames from the video; **A1** is the
  soundtrack's waveform. The amber **IN** and **OUT** bars set where the
  cue starts and stops: drag them, or put the playhead somewhere and press
  **I** or **O**. The small squares on the top edge of each track are the
  fade handles: drag them in to fade the picture or the sound. Hold
  **Shift** while dragging for fine control. The wheel zooms around the
  mouse, Shift+wheel scrolls, and double-clicking the ruler fits the whole
  file. Lighting triggers show as small marks under the ruler.

The In and Out points are shared by the picture and the sound, so they
always start and stop together. They're the same trims the audio editor
and the Sound section use. Before 1.0.4, trimming a video's sound didn't
trim its picture. Now it does.

Every change is an ordinary cue edit: it's saved with the show, the cue
plays it straight away, and **Undo** works in the editor (**Ctrl+Z**) or
the main window. The editor previews on its own and doesn't touch what's
on the projector.

##### Splicing: cutting sections out

In and Out trim the ends. To take a piece out of the **middle** (a false
start, a long intro, a verse you're not using), cut it:

1. With the **Select** tool (**V**), drag across the timeline to select the
   section. Shift-drag grows the selection; **Shift+I** and **Shift+O**
   set its start and end at the playhead.
2. Press **Delete** (or **Delete section** in the toolbar, or right-click
   it).

The cut shows hatched across both tracks with how much it removed, and
**Plays for** gets shorter. Picture and sound jump straight over it
together.

The **Razor** tool (**B**) works the way it does in other editors: click
the timeline to drop split points (**Ctrl+K** splits at the playhead),
then click between two of them to select that piece, and press Delete.
**Clear split points** removes them; they're only a guide and aren't saved.

- **Keep only this** sets In and Out around the selection and cuts nothing:
  the quick way to trim to one section.
- **Right-click a cut → Restore section** brings it back.
  **Restore all cuts** brings them all back.
- **Esc** clears the selection.

Cuts don't change the file. They're saved with the cue, every cut and
restore can be undone, and the picture and the soundtrack share them.
Converting the video to an audio cue keeps its cuts, and the audio cue
skips them too. (The audio editor doesn't show cuts yet: its preview plays
straight through them.) During the show:

- the **sound** jumps over each cut with a tiny dip (a few milliseconds)
  either side, so the join doesn't click;
- the **picture** seeks past each cut. Depending on how the video was
  encoded, that seek can show a brief hitch at the join; files with
  frequent keyframes (or an editing codec such as ProRes) are smoothest;
- **lighting triggers** inside a cut never fire.

##### Keys

| Key | Does |
|---|---|
| **Space** | Play / pause (wherever the focus is in the editor) |
| **I** / **O** | Set In / Out at the playhead |
| **Shift+I** / **Shift+O** | Start / end the selection at the playhead |
| **V** / **B** | Select tool / Razor tool |
| **Ctrl+K** | Split at the playhead |
| **Delete** or **Backspace** | Cut out the selected section |
| **Esc** | Clear the selection |
| **Home** / **End** | Go to the In / Out point |
| **←** / **→** | One frame back / on (**Shift**: one second) |
| **Ctrl+Z** / **Ctrl+Y** | Undo / redo |

Play starts from the In point if the playhead is outside the trim, and
stops at the Out point (or goes round again with **Loop** on).

#### The video's sound *(new in 1.0.4)*

A video cue plays its file's soundtrack through quewi's audio engine, the
same one audio cues use. Its **Sound** section in the Inspector has:

- **Play the video's sound:** on for new video cues. Shows saved before
  1.0.4 load with it **off**, so they play exactly as they used to. Tick it
  to hear their soundtracks.
- **Level, Pan, Fade in, Fade out, Output:** the same controls as an
  audio cue.
- **Edit sound…** opens the soundtrack in the audio editor, with effects,
  presets, EQ and trims.

The sound follows the picture. GO, Stop, Pause, Start (resume) and the
Inspector's scrubber drive both, and a Fade cue with **Gain** as its
parameter fades the soundtrack, while **Opacity** fades the picture.

#### Lighting triggers on a video

*New in 1.0.4.* A video cue can cue the lighting desk from marks on its
soundtrack. Press **Lighting triggers…** in the Sound section. If the
video's sound is off, the triggers still run, following the picture's
clock. Converting to an audio cue and back keeps them. See
[Lighting triggers](../using-quewi/lighting-triggers.md#video-cues).

#### Turning a video cue into an audio cue

**Convert to audio cue** (in the Sound section, or **Cue → Convert
Video ↔ Audio**) drops the picture and keeps the sound as an ordinary audio
cue. Use it when a song mix is a .mov or .mp4 but tonight you only need the
audio. The cue keeps its number, name, notes, waits and colour. Fade, Start
and Stop cues that target it still find it.

The audio cue remembers the video settings. **Convert back to video cue**
(in its Audio section, or the same menu item) restores the screen,
position, size and opacity. Any sound changes you made while it was an
audio cue are kept. Undo works for both directions.

Any audio cue whose file is a video (.mp4, .mov, .mkv…) can be turned into
a video cue the same way.

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
- **Frame-locked sync** between picture and sound. They start together
  and pause, seek and stop together, but run on separate clocks, so a
  long video can drift slightly. That's fine for song mixes with visuals,
  but not for lip-sync over long takes.
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
