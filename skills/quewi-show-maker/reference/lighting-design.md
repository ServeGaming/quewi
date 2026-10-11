# Timing lighting to music: how to design the cues

Rules of thumb a theatre lighting designer / programmer uses, turned into
what to do with the analysis. They're defaults; the user's taste wins.

## What gets a cue

1. **Music start** (`musicStart`, or the first strong hit if there's a
   count-in): the song's first look. Fire right on it; the look's fade time
   lives on the desk.
2. **Section changes** (`sections[].start` — already on a bar line): a new
   look where the music changes character. Same letter = same kind of music,
   so a returning chorus gets the same (or a bigger) look, not a new idea.
   - `level` high → fuller/brighter/more saturated; low → pulled in, fewer
     sources, cooler; mid → in between.
   - Skip a change shorter than ~4 bars unless it's a real moment (a
     2-bar break before the last chorus *is* one).
   - Typical density: 4–8 looks for a 3–4 minute song. More than one change
     per 8 bars reads as busy unless the user asked for it.
3. **Lifts and drops** (`jumps`): a lift (+6 dB or more) right after quiet is
   a "slam" — fire on the downbeat it lands on, with a fast desk time. A drop
   into near-silence is a pull-back; often the same moment as a section
   change already.
4. **Hits** (`hits`, strongest accents): candidates for **accents** — a sub
   bump or a sub to full and back (range trigger) — not for whole new looks.
   Pick the 2–6 that are clearly stabs (on the beat, in a loud section, or
   a band "hit" before a break). Off-beat hits (`*`) are often vocal
   consonants or drums fills: only use one if the user cares about it.
5. **Stops** (sudden silence mid-song): a classic "lights snap with the
   band" moment — a cue on the stop (often near-black or a single special)
   and another on the re-entry (`stops[].at`, then the next downbeat/hit).
6. **The end** — `ending.type` says which:
   - *button* (loud to a hard stop): blackout or a final "button" look
     exactly on `ending.lastHit`, 0 s.
   - *fade* (`ending.from` → `ending.to`): a slow fade cue at `from`; note
     "fade over ~N s" (`to - from`) for the desk time.
   - *ring* (a last hit left to decay): a cue on `lastHit`, then black at
     `silenceAt` (or a slow fade over the decay).
   Check the last bars of the loudness strip: a quiet tag after the last
   loud bar is its own small moment (often a pull-in before the button).
7. **Beat pulses** (fills): only in high-energy sections, only if the user
   wants movement. Every beat (fast pop/dance) or every 2nd beat / every bar
   (slower or heavier). Short holds (0.1–0.2 s) for bumps. One sub, one group
   per run so they can be adjusted together. Don't pulse under dialogue,
   ballads, or across a section change.

## Timing

- Trigger **on** the musical moment. The desk's cue time does the shaping:
  a 0 s cue lands *with* the hit; a build is a cue fired at the start of the
  build with a long time (note it: "4 bar build, ~7 s fade"). At tempo T, a
  bar is `beatsPerBar * 60 / T` seconds — give fade suggestions in bars and
  seconds.
- For a snap that must land exactly on a downbeat, fire 30–50 ms early
  (network + desk latency) only if the user says it's late on their rig;
  otherwise fire on the beat.
- Use `"bar N"` times in the plan whenever the moment is musical: they come
  from the beats actually heard, so they stay right when the tempo drifts.

## Which section is "the chorus"

Sections with the same letter are the same music. "The chorus" is usually
the loudest letter that repeats. "The big / final chorus" is usually the
last loud section — after a break, a drop or a key change (its tempo or
loudness often steps up). When the user names one, say which section you
took and why, and ask them to confirm by ear.

## Desk cue numbering (when the user hasn't decided)

Propose one block per song, with gaps for later inserts:

- Song in sound cue 2 → desk cues **201, 205, 210, 215…** (or 2.1, 2.2, … if
  they prefer decimals — Eos allows them).
- The song's **end state** (blackout or button look) is the last of its
  block: **299** for sound cue 2, 399 for 3, … — so with go-to-cue each
  song's cues stay together and in order on the desk.
- Preshow / walk-in states before the first song get low numbers (1–99).
- Put the song name and intent in the cue sheet's notes; the LD builds
  labels from them.

Use plain **GO** only when the user says the desk runs its own list in order
and nothing else fires cues on it. Go-to-cue is safer: one missed or extra GO
can't put the rest of the show out of step.

## What to tell the user about accuracy

The analysis finds tempo, bars and sections from the audio alone. It's
usually right for studio recordings; it can be off by a bar at a section
change in through-composed or rubato music, and beats can wander in live or
click-free recordings. Say which moments you're least sure of, and point
them at quewi's audio editor (Lighting lane) to nudge them — or at **Record
from desk**, which notes the moment they press GO on the desk while the song
plays.
