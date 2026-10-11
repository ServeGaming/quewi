---
name: quewi-show-maker
description: Make a quewi show file (.quewi) from music tracks, with lighting cues timed to the music. Use when someone gives you one or more songs/tracks (mp3, wav, flac, m4a, video) and wants a quewi show — sound cues in order, plus lighting triggers that fire the desk (Eos GO / go to cue / sub bumps on the beat / blackout on the button) at the right moments — written to a folder they name. Also use to add timed lighting to tracks, to time lighting cues to a song, to make a lighting cue sheet from a song, or to look inside an existing .quewi file.
---

# quewi show maker

Turns tracks into a ready-to-run quewi show: one audio cue per track, each
carrying **lighting triggers** — moments in the song where quewi tells the
lighting desk to do something (GO, go to a cue, bump a sub on the beat, set a
sub, fire a macro…). quewi sends them to whatever desk is set in its
**Tools → Lighting Desk…** (ETC Eos / Ion / Nomad, grandMA, MSC), so the show
file itself doesn't care which desk.

You do the musical listening with `scripts/analyze_track.py` (tempo, every
beat, bars, sections, hits, drops, fade-out), design the cues like a lighting
designer would, and write the file with `scripts/build_show.py`. Nothing is
ever sent to a desk; the user runs the show in quewi.

Scripts need Python 3 with numpy, and ffmpeg on the PATH (`ffmpeg -version`).
Run them by path from this skill's folder.

## 1. Get what you need (ask only what you can't infer)

- **The tracks** (paths) and their **running order**. Cues are numbered
  1, 2, 3… in list order, memos included (a "house to half" memo first makes
  the songs 2, 3…), unless the user has numbers they want.
- **Where to save** the show (a folder, or a full `….quewi` path) and its
  name. Never overwrite an existing show without asking.
- **How the desk is set up** — this decides the trigger actions:
  - *The desk's own cue list in order* (simplest): every trigger is a plain
    **GO**. Tell them the desk then needs exactly that many cues, in order.
  - *Specific desk cues* (most robust — a missed GO can't put the show out
    of step): **Go to cue** `list/number`. If they haven't numbered the
    desk yet, propose a scheme (see reference/lighting-design.md: one block
    per song, gaps for inserts) and use it.
  - *Bumps/accents*: which **sub** (or fader) to bump on hits or beats, if
    any. Don't invent sub numbers the desk doesn't have — ask, or leave
    bumps out.
- **Style**: how busy (a look per section? pulses on the beat in choruses?
  stabs on hits?), and anything that must happen at a known moment ("black
  on the last chord", "lights up when the vocal comes in").

If the user just says "do the lighting", default to: go-to-cue looks at
the music's start and at each section change, a bigger look for the loud
sections, nothing on the beat, and a blackout (or slow fade) at the end —
then show the plan before writing.

## 2. Listen to the tracks

```
python scripts/analyze_track.py TRACK1 TRACK2 … --out analysis.json
```

Read the printed summary for each track:

- **start and end**, and **how it ends**: *button* (loud to a hard stop —
  black on the "last hit" time), *fade* (a long steady fall — a slow cue over
  the fade), *ring* (a last hit left to decay);
- **tempo**, and whether it holds steady (live recordings and rubato don't);
  the **first beat** and **bar 1** (pickup beats before bar 1 are normal —
  the music can start before bar 1);
- **sections** A/B/C… (same letter = similar music, e.g. a returning chorus)
  with bars, their own tempo, and a loudness level (an *average*, so a short
  loud tag or a 1-bar drop inside a section won't show in it);
- **loudness per bar** (0–9 strip, bar 1 first): the check for exactly where
  a drop, break, tag or build lands — trust it over a section's level;
- **hits** (strongest accents, at least one per section; `*` = off the
  beat), **jumps** (lifts/drops of 6 dB+ bar to bar) and **stops**.

If the tempo looks half or double what the song obviously is, re-run with
`--bpm-hint`. Use `--bpb 3` for a waltz, `--bpb 6` for 6/8. The analysis is
an estimate: say so, and point big moments out for checking by ear.

## 3. Design the lighting

Decide, and say in the plan, the choices the user didn't make: the desk cue
numbers (reference/lighting-design.md has a default scheme, blackouts
included), which section is "the chorus" / "the big chorus" when they name
one (usually: the loudest repeated section; the *biggest* is usually the
last loud section, after a break or key change — flag it for them to
confirm), and whether to set a `beatGrid` (only when the analysis says the
tempo is steady).

Read **reference/lighting-design.md** and build a cue plan per song from the
analysis: which moments get a new look, which get accents, what the desk is
told each time, with a short intent note for the lighting designer ("warm
sunrise, low", "full stage, saturated"). Put trigger times on musical
moments: section starts are already on bar lines; use `"bar 27"` /
`"bar 27 beat 3"` in the plan and the build resolves them from the beats
heard.

**Show the plan before writing**: per song, a compact table — time, bar,
section, desk action, intent — plus totals ("7 looks, 1 pulse run of 16
bumps"). Adjust to feedback.

## 4. Write the show

Write the plan as JSON (format: **reference/plan-format.md**), then:

```
python scripts/build_show.py plan.json --analysis analysis.json --check
python scripts/build_show.py plan.json --analysis analysis.json
```

`--check` validates without writing (bad times, past-the-end triggers,
duplicate cue numbers, missing files, unknown actions). The build writes
the `.quewi` exactly as quewi saves one, plus **"<show> - lighting
cues.md/.csv"**: every desk cue the show calls, with times and notes — the
list to build on the desk. Add `--force` only if the user agreed to
replace an existing file.

Track files are referenced where they are (absolute paths). If the user
wants a self-contained show folder (to move to another machine), copy the
tracks into `<show folder>/audio/` first and point the plan at the copies.

## 5. Check and hand over

```
python scripts/inspect_show.py "path/to/Show.quewi" --triggers
```

It must end with `OK: quewi can open this show.` Then tell the user:

- where the show and the cue sheet are;
- per song: how many triggers and what they do, in one or two lines;
- **what to set in quewi**: Tools → Lighting Desk… (desk type, IP, port)
  if not already done; the triggers do nothing until a desk is set;
- **what to build on the desk**: the cue sheet's cues (if they want the
  desk side built too and have an Eos, the `eos-show-builder` skill can
  make those cues from the sheet);
- what to check by ear: open a cue in quewi's audio editor (double-click
  it) → the Lighting lane shows every trigger on the waveform; drag any
  that land early/late, or use **Record from desk** to tap them in live.

## Other jobs

- **Look inside a show**: `inspect_show.py SHOW.quewi --triggers` (add
  `--json` for everything).
- **Just a cue sheet / timings** (no show file): analyse, design, and give
  the table; build with `--check` to validate.
- **Change an existing show**: this skill writes new shows. To change one,
  inspect it, rebuild a plan with the changes (same order and numbers), and
  write it under a new name; the user can copy cues across in quewi.
