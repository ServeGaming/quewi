# Cue Types

Each cue type's parameters and behavior. See `design.md §8` for inspector sketches.

## Playback cues

### Audio
File path, in/out trim, fade in/out, loops, slices, master level, per-output matrix levels, integrations (other cues to start/stop/pan/level on this one's events).

### Mic
Live audio input with monitoring level and matrix routing. No file.

### Video
Geometry (x, y, w, h, rotation), opacity, blend mode, surface (which output / Spout/Syphon channel), in/out trim, loop, rate, audio matrix.

### Camera
Live camera input. As Video minus playback fields.

### Image
Geometry, opacity, surface. Static image.

### Text / Title
Rendered text overlay. Font, color, alignment, geometry.

## Control cues

### Fade
Targets another cue. Animates a parameter (level, geometry, opacity, level matrix) over a duration with an easing curve.

### Start / Stop / Pause / Load / Reset / Devamp
Operate on a target cue.

### Goto / Target
Jump to a cue. Goto continues playback from there; Target arms it as next.

### Arm / Disarm
Toggle a cue's armed state.

### Wait
Time delay only.

### Memo
A note in the cue list. Doesn't fire anything; useful as a section header.

### Group
Container for child cues. Modes:
- **Parallel** — fires all children on GO.
- **Sequential** — children act like a sub-cue list, one at a time, GO advances within the group.
- **Timeline** — children placed on a timeline with offsets.
- **Start first** — fires only the first armed child.
- **Start random** — fires a random armed child.

### Cue List
Triggers another cue list (used for sub-shows).

## Network cues

### OSC
Address, args (any OSC 1.1 type), destination from the patch. Optional bundle wrapper with time tag.

### MIDI
Channel message (note on/off, CC, program change, pitch bend) or sysex.

### MSC
MIDI Show Control: device id, command (GO, STOP, RESUME, TIMED GO, LOAD, SET, FIRE, ALL OFF, RESTORE, RESET, GO_OFF, GO/JAM_LOCK, STANDBY+/-, SEQUENCE+/-, START_CLOCK, STOP_CLOCK, ZERO_CLOCK, SET_CLOCK, MTC_CHASE_ON/OFF, OPEN_CUE_LIST, CLOSE_CUE_LIST, OPEN_CUE_PATH, CLOSE_CUE_PATH).

### Network (HTTP)
Method, URL, headers, body. Async; result logged.

### Script (Phase 7+)
Lua snippet with bindings to the workspace, cue list, engines.

## Lighting cues

### Light
Per-channel scene over patched universes. Snapshot or delta.

### Light Fade
Targets a Light cue. Fades from current state to target over duration.

## Timecode cues

### MTC Generate / Chase
Send or receive MIDI Timecode, locking other cues to it.

### LTC Generate / Chase
Same but Linear Timecode over audio.
