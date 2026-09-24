# quewi Mix (DCA mixing)

quewi Mix is TheatreMix-style DCA mixing built into quewi. For each scene
you say who's on which DCA fader. When you fire the cue, quewi assigns
those mics to those DCAs on the console and mutes the show mics that aren't
in the scene. You mix the scene on a handful of DCA faders instead of
chasing forty channel faders.

**It never touches your fader levels.** quewi does the bookkeeping (which
mics are on which DCA, and what's muted), and you own the mix.

Supported consoles:

- **Behringer X32 / Midas M32** (OSC over UDP)
- **Yamaha DM7** (RCP over TCP)

---

## Setting up

1. **View → Mix (DCA) grid** (`Mod + Shift + M`) adds a Mix list to the
   show and opens it.
2. **Channels & ensembles…** is where you name your mics once: strip number,
   character name, actor, backup. **Ensembles** group mics ("Ensemble
   Women", "Orchestra") so one cell can assign twenty of them. Edit an
   ensemble or rename it, and every cue that uses it follows.
3. **DCAs:** sets how many DCA columns the show uses.
4. Under **Console:** choose the desk type, type its IP address and
   press **Connect**. The status reads *Connected — model* once the
   console has answered.

quewi only ever changes the strips you've named in **Channels &
ensembles**. Anything else on the desk (band inputs, playback returns,
talkback) is left alone.

---

## Writing cues

Each row is a scene and each column is a DCA. To say who's on a DCA,
**double-click the cell**. A picker lists every mic and ensemble in the
show, and you tick the ones you want. If the show has no mics yet, the
picker offers a shortcut into **Channels & ensembles**. Cue numbers and
names edit inline.

The grid highlights what each cue changes compared to the one before it.
New arrivals stand out from mics that are leaving, so you can see what a
GO will do before you press it.

**Add cue** / **Delete cue** manage the rows.

---

## Running

- The Mix page's own **GO** applies the selected cue to the console and
  moves to the next one.
- The **DCA GO** button in the main transport bar does the same from
  anywhere in quewi, so you don't have to switch to the Mix page mid-show.
  It's greyed out, with a tooltip saying why, until a console is
  connected.
- The live cue (the one the console is currently on) is marked in the grid.

### Linking a sound cue to a DCA cue

In the [Inspector](inspector.md), every cue has a **Linked DCA cue** field,
shown once the show has mix cues. Pick a DCA cue,
and firing either one fires both: one GO moves the scene and plays the
sound. It works from either end, and a linked pair never loops back on
itself.

---

## Good to know

- The Mix list shares the show file with everything else. Save once and
  it's all there.
- If the console drops off the network, quewi notices and shows it next
  to the **Connect** button.
- **X32:** develop and rehearse against the free X32 emulator. quewi's test
  suite runs against it too.
- **DM7:** DCA assignment is supported. Channel EQ control is not yet, as
  it's waiting on a hardware check.
