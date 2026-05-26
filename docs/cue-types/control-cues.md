# Control cues — Start, Stop, Goto, Pause, Load, Reset, Devamp

Cues that act on *other* cues. All share the same inspector
field — `targetId` — and differ only in what they do to the
target.

## The full set

| Cue type | Key | What it does |
|---|---|---|
| **Start** | <kbd>Shift</kbd>+<kbd>S</kbd> | Fires the target cue (same as GO'ing the target manually) |
| **Stop** | <kbd>Shift</kbd>+<kbd>X</kbd> | Stops the target cue if it's playing |
| **Goto** | <kbd>Shift</kbd>+<kbd>G</kbd> | Moves the queue pointer to the target — next GO fires that cue |
| **Pause** | — | Pauses the target (audio voices freeze at their read position) |
| **Load** | — | Pre-loads the target without firing — useful for big audio files |
| **Reset** | — | Returns the target to its initial state without firing |
| **Devamp** | — | Exits a vamp / loop on the target so the next GO advances normally |

## Inspector

Just one field:

| Field | Meaning |
|---|---|
| **Target** | UUID of the cue to act on |

Pick the target from the dropdown — quewi lists every cue in the
workspace.

## Use cases

### Start — chain cues across lists

Most useful between cue lists. Cue `5` in the Main list fires a
Start cue targeting cue `1` in the "FX" list — both lists are
now in motion.

### Stop — clean ending for a specific track

Lyrical interlude cue and you want it to stop precisely on a
beat:

```
2.5 Audio  song.mp3   [loop, AutoContinue]
2.6 Wait    8.5 s     [AutoContinue]
2.7 Stop    → 2.5
```

Plays the song, waits 8.5 s, stops it. Cleaner than a Fade with
a hard cut-off.

### Goto — non-linear shows

In a sketch-comedy show where the order changes night-to-night,
the operator can leave a Goto cue at the top of every sketch
section. The night's order is set by GO'ing the Goto cues in
the right sequence.

### Load — pre-warm before a hot cue

For a critical cue that has to fire with no latency:

```
4.0 Load  → 5.5   [AutoContinue]
4.1 (other stuff)
5.5 Audio big-file.wav
```

Load at 4.0 pre-decodes big-file.wav so cue 5.5 fires from
warmed-up RAM.

### Reset — return a cue to its pre-fire state

After a vamp loop, Reset returns the target's read position to
zero so the next GO plays from the top.

### Devamp — exit a loop

A music cue authored as `loop: true` keeps playing forever. A
Devamp cue clears the loop flag on the playing voice — the next
loop boundary is the last; playback ends naturally.

## Combinations

Targeting cues are powerful when combined. A "vamp the bumper
music until I press GO" pattern:

```
3.0 Audio  bumper-music.wav   [loop]      (fired, vamping)
3.5 Devamp → 3.0              [AutoFollow] (exit loop on GO)
3.6 Wait until natural end                (cue 3.5's AutoFollow waits)
3.7 (next thing)
```
