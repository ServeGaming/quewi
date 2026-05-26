# Fade cue

Animates a numeric field of another cue from its current value
to a target value over a duration.

## Inspector fields

| Field | Type | Meaning |
|---|---|---|
| **Target** | UUID | The cue whose field is faded |
| **Parameter** | string | The field name to animate (e.g. `gainDb`, `opacity`, `pan`) |
| **Target value** | double | Value at the end of the fade |
| **Duration** | seconds | How long the fade takes |

## What it can fade

Any numeric field on any cue type. Common uses:

- **Audio gain** — `gainDb`. Smooth fade-down for ducking dialogue
  under music; fade-up for an entrance.
- **Audio pan** — `pan`. Stereo sweeps.
- **Visual opacity** — `opacity`. Cross-fade between two video
  cues.
- **Visual size / position** — `posW`, `posX`, etc. Zoom or
  reposition during playback.

## What it can't do

- **DMX channels** — use a Light Fade cue instead, which fades
  channels en masse.
- **Strings, file paths, booleans** — only numeric fields fade.

## Continue behaviour

A Fade cue's "finished" event fires when the animation reaches
the target value. AutoFollow on a Fade cue chains the next cue
the moment the fade completes — useful for "fade out, then
black" sequences:

```
3.5  Fade  music.wav → -∞   over 5 s   [AutoFollow]
3.6  Light blackout                    [DoNotContinue]
```

## OSC

The Fade cue itself is editable via standard
`/quewi/cue/<n>/set/<field>`. See the
[OSC field reference](../osc-control/field-reference.md).
