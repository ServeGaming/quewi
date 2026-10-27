# Vamps, repeats, holds and skips

*New in 1.3.0.*

Performance tracks (Concord / Right On Cue's Stage|Tracks, MTI and others)
are recorded with **vamps**: a few bars the band would repeat until the
actors are ready. quewi plays them the same way. Mark the vamp on the song,
and in the show the music loops there until you press **Advance**, then
carries on exactly as the band would.

Everything here lives on an **audio cue**, in its editor. Four kinds, all
moved on by one **Advance** button (<kbd>→</kbd>):

| | What it does | Advance |
|---|---|---|
| **Vamp** | Loops a section until you press Advance. | Leaves the vamp (see *Exit* below). |
| **Repeat** | A vamp with a count: plays the section N times, then goes on by itself. | Not needed (ignored). |
| **Hold** | Stops the music, silent, at a point (a caesura / fermata). | Goes on from where it stopped. |
| **Skip** | A section that plays through *unless* you press Advance inside it. | Jumps to its end. |
| **Marker** | A labelled point ("Letter B", "Dance break"). | — |

Cuts (sections removed for good) are the [video editor's cuts](../cue-types/video.md),
which audio cues have too.

## Making them

Double-click an audio cue to open its editor. Under the LIGHTING lane is the
**VAMPS** lane:

- **Drag across the lane** to make a vamp over that span. With the song's
  beat grid set (Lighting tab → tempo), it snaps to beats, so a vamp lands on
  bar lines.
- **Right-click** the lane for a vamp, repeat, hold, skip or marker at that
  point (one bar long, or 2 s without a grid). Right-click one to change its
  kind, its exit, rename or delete it.
- Drag one to move it, drag its edges to resize it; <kbd>Delete</kbd> removes it.
- The **Vamps** tab (beside Effects and Lighting) lists them all with their
  times (and bar/beat with a grid), repeat counts and exits, to type exact
  values. **+ Vamp / + Repeat / + Hold / + Skip / + Marker** add one at the
  playhead. Double-click a row to move the playhead there.

Every change is one undo step. Vamps can't overlap each other.

### Exit: when a vamp lets go

Each vamp says how it leaves when you press Advance:

- **At the end of the pass** (default): the band finishes the vamp, then
  goes on. Press Advance any time during the last pass you want.
- **At the next bar**: leaves at the next bar line (needs the beat grid), so
  a long vamp doesn't make the actors wait for the whole of it.
- **Now**: leaves straight away, to the music after the vamp.

The loop point is crossfaded (a few milliseconds), so a vamp sounds as
seamless as the recording allows. Lighting triggers inside a vamp fire on
every pass (a bump on the beat keeps going); put a desk GO *after* the vamp,
not inside it.

## Running them

While a song with vamps, holds or skips plays:

- the **ADVANCE** button appears beside GO in the main window. Greyed, it
  counts down to what's coming ("Vamp in 0:12"); lit, it says what's waiting
  ("Vamp · pass 3", "Hold", "Skip"). <kbd>→</kbd> does the same (Keyboard
  shortcuts: *Advance*). The arrow key only does this while something is
  waiting, so it's free for lists and grids the rest of the time;
- **Show Mode** has ADVANCE under GO, and the running song shows
  `VAMP · 3` / `HOLD` / `SKIP` / `REPEAT 2/4` in place of its time (the time
  left reads "—" while it vamps or holds);
- the [safe key](shortcuts.md#safe-key), when it guards GO, guards Advance
  too.

If two songs are waiting, Advance takes the most urgent: a hold, then a vamp,
then a skip — the newest song first.

A **Devamp** cue does Advance for one cue (handy to put in the cue list, or
to fire from an auto-continue), and also ends a cue that loops as a whole
(*Loop* on): it finishes the pass it's on and plays out.

From a remote or QLab: `/quewi/advance`, or `/quewi/cue/<number>/advance`
for one cue ([OSC reference](../osc-control/reference.md#vamps-and-advance)).

## Coming from Stage|Tracks

| Stage\|Tracks | quewi |
|---|---|
| Vamp (Vamp Count 0) | Vamp |
| Vamp Count N / Repeat Count N | Repeat ×N |
| Caesura | Hold |
| Skip | Skip |
| Cut | Cut (video editor's cuts, on audio cues too) |
| Marker | Marker |
| Advance / → | Advance / → |
| Playback state: auto-continue / queue / skip | Continue: auto-continue or auto-follow / don't continue / disarm the cue |
| Fade Out button | Fade All, or a Fade cue |
| Transpose | the cue's **Pitch Shift** effect (whole song) |
| Tempo Scale | not yet |
