# Lighting triggers

*New in 1.0.4.*

Lighting triggers let a song cue the lighting desk. You put marks on an
audio cue's song, and when the playhead reaches one, quewi tells the desk
to do something: press GO, go to a cue, bump a sub on the beat, fire a
macro, or fire another cue in your show. Mark the downbeat of the chorus
once, and the desk goes on that beat every night, whatever time the
operator pressed GO.

You don't need to know any OSC. Tell quewi which desk you have once, then
pick what each trigger does from a list. For anything the list doesn't
cover, **Custom OSC / MIDI** lets you type the exact message.

Video cues have triggers too. They sit on the video's soundtrack (see
[Video cues](#video-cues) below).

---

## Setting up the desk

Do this once per computer: **Tools → Lighting Desk…** (also under
**Preferences → Lighting**). Pick your desk, and the window shows what to
type in and what to turn on at the desk end.

| Desk | Connection | Settings |
|---|---|---|
| **ETC Eos / Ion / Element / Nomad** | OSC over the network | **Desk IP address** and the **Desk's OSC receive port**. |
| **grandMA3** | OSC over the network | **Desk IP address**, **Port** and an optional **OSC prefix** (such as `gma3`). |
| **grandMA2 / MSC** | MIDI Show Control over a MIDI cable | **MIDI output** and **MSC device ID** (127 = all-call). |

The desk setting belongs to **this computer**, not the show. Take the
show to another venue and you set the desk there, without touching any
cue. Every trigger that talks to "the desk" follows it.

!!! tip "ETC Nomad on the same computer"
    Use **127.0.0.1** as the desk IP. On Nomad, open **Setup → System →
    Show Control**, turn **OSC RX** on, and set the **OSC UDP RX port** to
    the same port as in quewi.

    Ideally that port isn't the same as quewi's own OSC listen port
    (**Preferences → OSC**, 8500 by default). When two programs on one
    computer listen on the same UDP port, a message may reach either one.
    If triggers go missing, give Nomad a different port and set the same
    one here.

### Reading the desk back

*Eos family only.* With **Show what the desk is doing** ticked in the
Lighting Desk window (it is by default), quewi also connects to the desk's
OSC **TCP** port (3032) and reads its running cue, its next (pending) cue,
the running cue's progress, its show name and whether it's in Blind. That
shows up in the **Lighting panel** and in [Show Mode](show-mode.md). It only
reads, so it never changes anything on the desk, and if the link drops quewi
keeps trying to reconnect.

On the desk, OSC TCP just needs to be allowed, which it is unless someone
has turned it off. Nomad on the same computer works with 127.0.0.1.

---

## The Lighting panel

**View → Lighting panel** opens a panel you can dock beside the cue list
(either side, or along the bottom; drag its title bar) or float on another
screen. It shows:

- the desk's link and its **ACTIVE** and **PENDING** cues, with progress;
- the **next lighting hits** in whatever's playing, each with a live
  countdown, what it will do, and which song it's in;
- an **ARMED** switch (the same as Tools → Lighting Triggers Armed);
- **Desk settings…** for the Lighting Desk window.

Show Mode shows the same things, larger.

---

## Points and ranges

A trigger is one of two things:

- **A point** is a single hit at one moment. It does one thing when the
  song plays past it.
- **A range** covers a section of the song, for example the whole chorus.
  It does one thing **on enter** (as the song reaches its start) and
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
  triggers the cue has. On a video cue it's in the **Sound** section (and
  the video editor's **Lighting** section).
- or open the audio editor any other way and pick the **Lighting** tab at
  the bottom (next to **Effects**).

### The Lighting tab

The top line shows which desk triggers go to, with **Change…** to set it.
Under it is the [beat grid](#beat-grid). The left side lists the song's
triggers: on/off, name, start, end and what each one does. Above the list:

- **+ Point here** adds a point at the playhead (where the song is
  playing, or where you last clicked).
- **+ Range** adds a 4-second range starting at the playhead.
- **Duplicate** and **Delete** act on the selected trigger.

Select a trigger to edit it on the right: name, start, end, **Range**,
**Enabled**, and what it does. A point has one **Sends** card. A range has
**On enter** and **On exit** cards.

!!! tip "Placing hits while the song plays"
    Play the song in the editor and press **+ Point here** on each hit.
    **Space** plays and pauses the editor wherever the keyboard focus is,
    so you can keep a hand on the mouse.

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

With a beat grid and **Snap** on, everything you place or drag lands on
the nearest beat. Hold **Alt** to place it freely.

Every edit, in the lane or the tab, can be undone, and triggers are saved
with the show.

### Editing several at once

Select more than one trigger and the editor changes them all together.
The editor's Name and times are greyed out, and a line above it
says how many are selected.

**Selecting:**

| Where | Do this | To |
|---|---|---|
| Lane | **Ctrl**+click | Add or remove one trigger |
| Lane | **Shift**+click | Add a trigger (and its group) |
| Lane | **Shift**+drag (or **Ctrl**+drag) across empty space | Add every trigger in that stretch |
| List | **Ctrl**+click, **Shift**+click, **Ctrl+A** | The usual list selection |

**Editing:** change anything in the **Sends** card and the change goes to
every selected trigger. Only the setting you touched changes. Say twelve
beats bump subs 1, 2, 3 and 4 in turn: set **Hold** to 0.1 s, and all twelve
get the new hold while each keeps its own sub. Pick a different **What
should it do?** and they all switch. The card shows the trigger you picked
last.

**Also on a selection:**

- **Drag** any selected marker in the lane and they all move together,
  keeping their spacing. The one you grab snaps to the beat.
- **Delete** (the button, the Delete key, or right-click) removes them all.
- **Enabled**, and **Enable / Disable** on the right-click menu, switch them
  all on or off.
- **Duplicate** copies the block straight after itself, on the next bar
  when there's a beat grid, so a chorus pattern repeats in time for the
  next chorus. The copy is selected, ready to drag.

Every one of these is a single undo step.

### Groups

A **group** keeps triggers together so you can come back and edit them as
one later. **Fill with beats** puts its new beats in a group named after
them ("Beat", then "Beat 2" for the next fill…).

- **Click** any trigger in a group in the lane and the whole group is
  selected. Drag it and the group moves. **Alt**+click picks just that one.
- To make a group, select the triggers and press **Ctrl+G** (or right-click
  → **Group…**) and give it a name, such as "Chorus bumps". Or type a name
  in the editor's **Group** box.
- Right-click a trigger → **Select Group** or **Ungroup**. In the editor,
  **Select all** next to the Group box selects the rest of the group.
- The list has a **Group** column.

A trigger can be in one group at a time. Groups are saved with the show.

---

## What a trigger can do

Each card asks **What should it do?** Pick from the list and fill in the
number or two it asks for. The list only offers what your desk can do.

| Choice | Does | Eos | grandMA3 | grandMA2 / MSC |
|---|---|:-:|:-:|:-:|
| **GO (next cue)** | Presses GO on the main cue list | ✓ | ✓ | ✓ |
| **GO on another cue list** | GO on a different list (see below) | ✓ | ✓ | |
| **Stop** | Presses Stop | ✓ | | ✓ |
| **Back** | Presses Back | ✓ | ✓ | |
| **Go to cue** | Goes to a cue number (and **Cue list**) | ✓ | ✓ | ✓ |
| **Set sub level** | Sets a sub to a **Level** (0–100 %) | ✓ | | |
| **Bump sub** | Presses a sub's bump button and lets go after **Hold** seconds | ✓ | | |
| **Set fader level** | Sets a fader (**Fader**, **Fader page**) to a **Level** | ✓ | | |
| **Bump fader** | Presses a fader's bump button for **Hold** seconds | ✓ | | |
| **Fire macro** | Fires a macro | ✓ | | ✓ |
| **Desk command** | Types a command line, such as `Chan 1 At Full`, and presses Enter | ✓ | ✓ | |
| **Fire a quewi cue** | Fires a cue in this show, as if you pressed GO on it | ✓ | ✓ | ✓ |
| **Nothing** | Leaves the edge empty (handy for a range that only needs an enter) | ✓ | ✓ | ✓ |

A trigger can't fire its own song (that would restart it and fire the
trigger again, forever). quewi refuses and says so in the status bar.

### GO on another cue list

**Eos:** load the cue list on a fader on the desk, then put that
**Fader** and **Fader page** in the trigger. quewi presses that fader's
GO button. (Eos has no command that means "GO on list 2" without a fader:
`Go_CueList` on the command line selects channels instead.)

**grandMA3:** give the **Sequence** number. quewi sends `Go+ Sequence n`.

### Custom OSC / MIDI

Tick **Custom OSC / MIDI** to type the message yourself. Pick the kind:

| Kind | Sends | Settings |
|---|---|---|
| **OSC** | One OSC message | **Host** and **Port** (leave them blank to send to the desk set in **Tools → Lighting Desk…**), **Transport** (UDP, TCP or WebSocket), **Address**, **Args**. Args work like an OSC cue's: comma-separated and typed automatically (`42` is an int, `1.5` a float, `"text"` or `text` a string, `true`/`false`). |
| **MIDI** | A note on, note off, control change, program change, or raw bytes | **Port** (empty = the first available), **Type**, **Channel** (1–16), note/controller/program and velocity/value, or **Raw hex** such as `90 3C 7F`. |
| **MSC** | A MIDI Show Control command | **Port**, **Device ID** (127 = all-call), **Command** (GO, STOP, RESUME, TIMED GO, LOAD, SET, FIRE, ALL OFF, RESTORE, RESET, GO OFF), **Format** (Lighting, Moving lights, All types), **Cue number**, **Cue list** (optional). |

**Presets ▾** fills in a common message, then you change the numbers to
suit. It leaves **Host** and **Port** blank, so the message goes to your
desk.

| Preset | Fills in |
|---|---|
| ETC Eos — Fire cue (list/cue) | OSC `/eos/cue/1/1/fire` (list 1, cue 1) |
| ETC Eos — GO | OSC `/eos/key/go_0` |
| ETC Eos — Stop | OSC `/eos/key/stop` |
| ETC Eos — Back | OSC `/eos/key/back` |
| ETC Eos — Fire macro | OSC `/eos/macro/1/fire` |
| ETC Eos — Sub level | OSC `/eos/sub/1` with argument `1.0` |
| grandMA3 — OSC command | OSC `/gma3/cmd` with argument `"Go+ Sequence 1"` |
| grandMA — MSC GO cue | MSC GO, Lighting format, cue 1, list 1 |
| grandMA — MIDI note | MIDI note on 60, velocity 127, channel 1 |

Untick **Custom OSC / MIDI** to go back to the list.

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

## Beat grid

Give a song its tempo and quewi draws a line on every beat (stronger on
each bar), snaps triggers to them, and can fill a stretch of the song with
one trigger per beat. That's how you bump a sub in time with the music.

The **TEMPO** row in the Lighting tab:

| Control | Does |
|---|---|
| **BPM** | The tempo. **Off** (0) means no beat grid. |
| **Tap** | Tap along to the beat (or press **T**). While the song is playing, tapping also sets where the first beat is. |
| **Detect** | Listens to the song and finds its tempo and first beat. It works best on music with a clear, steady beat. If it isn't sure, it says so: tap instead. |
| **First beat** | Where beat 1 of bar 1 falls in the song. **Set to cursor** puts it at the playhead. |
| **Beats/bar** | Beats in a bar (4 for most songs). |
| **Snap** | Markers you place or drag land on the beat. Hold **Alt** to place freely. Remembered on this computer. |
| **Fill with beats…** | Adds a trigger on every beat (see below). |

The grid is saved with the cue, and editing it can be undone.

### Fill with beats

**Fill with beats…** adds a row of points in one go:

- **Range:** the **Selected range** (select a range trigger first, such as
  your chorus), **From cursor for** a number of bars, or the **Whole song**.
- **Every:** every **Beat**, every **2 beats**, or once a **Bar**.
- **Name:** they're named "Beat 1", "Beat 2"… (change the word if you like).
- **Each one sends:** what every new point does, for example **Bump sub**
  1 with a short **Hold**.

The dialog shows how many it will add before you press **Add**. The whole
fill is one undo step. The new beats are put in their own
[group](#groups) and selected, so you can change them all straight away.

!!! example "Bump sub 5 on every beat of the chorus"
    1. Set the tempo (type the BPM, **Tap** or **Detect**) and check the
       beat lines sit on the kicks.
    2. Drag across the chorus in the lighting lane to make a range.
    3. **Fill with beats…** → **Selected range**, every **Beat**, each one
       does **Bump sub**, Sub **5**, Hold **0.1** s → **Add**.
    4. Tick **Send while previewing** and play the chorus to watch it on
       the desk.

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
- **Cut sections** (see [Splicing](../cue-types/video.md#splicing-cutting-sections-out))
  are skipped: a trigger inside a section that's been cut out never sends.
- A **bump** lets go of the button after its **Hold** time, even if the
  song stops first, so a sub is never left stuck on.

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

Remotes can list, add, edit, remove and test triggers, arm and disarm,
read and change the lighting desk, set the beat grid, and watch triggers
fire. See
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
