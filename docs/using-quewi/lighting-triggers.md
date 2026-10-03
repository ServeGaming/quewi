# Lighting triggers

*New in 1.0.4.*

Lighting triggers let a song cue the lighting desk. You put marks on an
audio cue's song, and when the playhead reaches one, quewi sends something
to the desk: an OSC message, a MIDI note, an MSC GO, or it fires another cue
in your show. Mark the downbeat of the chorus once, and the desk goes on
that beat every night, whatever time the operator pressed GO.

Video cues have them too. They sit on the video's soundtrack (see
[Video cues](#video-cues) below).

---

## Points and ranges

A trigger is one of two things:

- **A point** is a single hit at one moment. It sends one thing when the
  song plays past it.
- **A range** covers a section of the song, for example the whole chorus.
  It sends one thing **on enter** (as the song reaches its start) and
  another **on exit** (as the song leaves it). Use a range when the desk
  should know it's *in* a section, such as bringing a sub up at the start
  and down at the end.

Trigger times are **seconds in the song file**, not in the cue. Changing
the cue's trim in or trim out doesn't move them. A trigger before the
cue's trim in never plays, because playback starts after it.

Each trigger also has a **name** (shown in the lane and in notifications)
and an **Enabled** switch. A disabled trigger stays where it is but sends
nothing.

---

## Making triggers

Triggers are edited in the audio editor. To open it on the right tab:

- select the cue and press **Lighting triggers…** in the Inspector. On an
  audio cue it's on the **Lighting** row, which also shows how many
  triggers the cue has. On a video cue it's in the **Sound** section.
- or open the audio editor any other way and pick the **Lighting** tab at
  the bottom (next to **Effects**).

### The Lighting tab

The left side lists the song's triggers: on/off, name, start, end and what
each one sends. Above the list:

- **+ Point at cursor** adds a point at the editor's cursor.
- **+ Range** adds a 4-second range starting at the cursor.
- **Duplicate** and **Delete** act on the selected trigger.

Select a trigger to edit it on the right: name, start, end, **Range**,
**Enabled**, and what it sends. A point has one **Sends** card. A range has
**On enter** and **On exit** cards.

### The lighting lane

The timeline has a thin **LIGHTING** lane under the ruler. It shows every
trigger as a marker (points) or a bar (ranges), and you can work in it
directly:

| Do this | To |
|---|---|
| Click an empty spot | Add a point there |
| Drag across an empty stretch | Add a range over it |
| Drag a marker or bar | Move it |
| Drag either end of a range | Resize it |
| Double-click a trigger | Rename it |
| Right-click a trigger | **Rename…**, **Make a Point** / **Make a Range**, **Enable** / **Disable**, **Delete** |
| Right-click an empty spot | **Add Trigger Here** |

Every edit, in the lane or the tab, can be undone, and triggers are saved
with the show.

---

## What a trigger can send

Pick the kind in the card's drop-down:

| Kind | Sends | Settings |
|---|---|---|
| **Nothing** | — | The edge is left empty (handy for a range that only needs an enter). |
| **OSC** | One OSC message | **Host**, **Port** (8000 by default), **Transport** (UDP, TCP or WebSocket), **Address**, **Args**. Args work like an OSC cue's: comma-separated and typed automatically (`42` is an int, `1.5` a float, `"text"` or `text` a string, `true`/`false`). |
| **MIDI** | A note on, note off, control change, program change, or raw bytes | **Port** (empty = the first available), **Type**, **Channel** (1–16), note/controller/program and velocity/value, or **Raw hex** such as `90 3C 7F`. |
| **MSC** | A MIDI Show Control command | **Port**, **Device ID** (127 = all-call), **Command** (GO, STOP, RESUME, TIMED GO, LOAD, SET, FIRE, ALL OFF, RESTORE, RESET, GO OFF), **Format** (Lighting, Moving lights, All types), **Cue number**, **Cue list** (optional). |
| **Fire cue** | Fires a cue in this show, as if you pressed GO on it | **Cue**. A trigger can't fire its own song (that would restart it and fire the trigger again, forever); quewi refuses and says so in the status bar. |

### Presets

**Presets ▾** fills in a common lighting-desk message. It sets the kind,
port, address and numbers, then you change the numbers to suit. It never
touches the **Host**, because that's your desk's address and you set it
once.

| Preset | Fills in |
|---|---|
| ETC Eos — Fire cue (list/cue) | OSC `/eos/cue/1/1/fire` on port 8000 (list 1, cue 1) |
| ETC Eos — GO | OSC `/eos/key/go_0` |
| ETC Eos — Stop/Back | OSC `/eos/key/stop_back` |
| ETC Eos — Fire macro | OSC `/eos/macro/1/fire` |
| ETC Eos — Sub level | OSC `/eos/sub/1` with argument `1.0` |
| grandMA3 — OSC command | OSC `/gma3/cmd` with argument `"Go+ Sequence 1"` |
| grandMA — MSC GO cue | MSC GO, Lighting format, cue 1, list 1 |
| grandMA — MIDI note | MIDI note on 60, velocity 127, channel 1 |

!!! tip "Setting up the desk"
    **ETC Eos / Nomad:** turn on OSC **UDP** receive on the desk and set
    its receive port to **8000** (the presets' port). Then put the desk's
    IP address in each trigger's **Host**. The exact menu path depends on
    your Eos version, so check the desk's manual (look for the OSC or "Show
    Control" settings).

    **grandMA:** for the MIDI and MSC presets, connect a MIDI interface (or
    a virtual MIDI port) from the quewi computer to the desk, and turn on
    MIDI or MSC **input** on the desk. For MSC, match the device ID and
    the cue list. For OSC to grandMA3, set up an OSC input on the desk and
    match the port. Again, the desk's manual has the menu paths.

### Testing

Each card has a **Test** button that sends that edge right now, so you can
check the desk reacts before you play the song. Test sends even when
triggers are disarmed (see below).

### Send while previewing

The audio editor's toolbar has **Send while previewing**. With it on,
playing the song in the editor sends its triggers as the preview passes
them, so you can program the desk against the music without running the
cue. It's off each time you open the editor, so previewing a song doesn't
drive the rig by surprise. Turning it off while inside a range sends that
range's exit.

---

## Arming

**Tools → Lighting Triggers Armed** is the master switch. Turn it off and
cues still play, but no trigger sends anything during playback. It's a
setting for **this computer**, not for the show, and it stays the way you
left it (armed by default). Use it to rehearse sound without the lighting
desk taking cues, or when you're running the show on a laptop away from
the rig.

The Test buttons and **Send while previewing** aren't affected by the
switch. They're things you do on purpose.

---

## How triggers behave during a show

- **Playing normally**, a point sends when the song passes it. A range
  sends its enter as the song reaches its start, and its exit as the song
  reaches its end. A range shorter than one update still sends enter
  before exit.
- **At GO**, a point sitting exactly where the song starts (its trim in)
  goes out with the GO, and a range the song starts inside sends its
  enter.
- **Seeking** (the scrubber, an OSC seek) skips the points in between,
  so jumping ahead doesn't fire a burst of old cues. Ranges catch up:
  landing inside one sends its enter, and leaving one sends its exit, so
  the desk matches the section you landed in.
- **Looping** songs replay their triggers every time round, including a
  hit right on the loop point.
- **Pausing** holds everything. Nothing sends until you resume.
- **Stop**, a fade-out finishing, or the song reaching its end sends the
  exit of any range you were inside.
- **Panic** stops tracking at once and sends nothing more to the desk,
  not even range exits.
- A trigger that fires a cue fires it like GO. It can't fire its own song.

When a trigger sends, the status bar shows what it sent (or why it
failed), its marker flashes in an open audio editor, and OSC remotes get
`/quewi/notify/trigger/fired`.

---

## Video cues

A video cue's triggers live on its **soundtrack**, so you edit them through
**Lighting triggers…** or **Edit sound…** in the video cue's **Sound**
section.

- A video cue that **plays its sound** runs its triggers on the
  soundtrack, exactly like an audio cue.
- A **silent** video (sound turned off) still runs them, following the
  picture's clock instead.
- **Converting** a video cue to an audio cue and back keeps its triggers.
  See [Turning a video cue into an audio cue](../cue-types/video.md#turning-a-video-cue-into-an-audio-cue).

---

## Over OSC

Remotes can list, add, edit, remove and test triggers, arm and disarm, and
watch them fire. See
[Lighting triggers in the OSC reference](../osc-control/reference.md#lighting-triggers-v5).

---

## Known limits

- **Timing.** Triggers are checked on the quewi user interface's clock
  every 10 ms, so a send is typically within about 15 ms of its mark.
  Because quewi follows the position the audio engine is *reading*, sends
  land roughly **50 ms before** you hear that moment from the speakers.
  That suits most desks (they have their own latency too); if you need a
  hit to land later, move the mark a little later. The very first play of
  a video that was just loaded can be late by about 0.3 s while the file
  opens.
- **Busy computer.** A very busy computer can delay the clock. quewi
  treats a late update as normal playback (not a seek), so points still
  fire, just late.
- **Editor times assume one region at the start.** The lighting lane
  shows times as seconds from the start of the editor's timeline, which
  matches the song when the song is a single region starting at 0 (the
  normal case). If you move or trim regions in the editor, the marks
  shift relative to what you hear in the editor. Place triggers before
  rearranging regions, or check them afterwards.
- **Add triggers before you GO.** Triggers are picked up when a cue
  starts. A cue that had none when it started won't send triggers you add
  while it's playing until its next GO. (Edits to a cue that already had
  triggers apply straight away.)
