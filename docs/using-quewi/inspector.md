# Inspector & editing

The right-hand panel is the **Inspector**. It edits whatever cue
is selected in the list. Select nothing and you get the empty
state with shortcut hints; select something and the Inspector
populates with that cue type's controls.

---

## Layout

Top to bottom:

1. **Type label** — "Audio Cue", "Light Cue", etc.
2. **Common fields** — number, name, pre/post-wait, continue
   mode, notes, armed, color. Once the show has a
   [Mix (DCA) list](mix.md), a **Linked DCA cue** field appears too.
   Pair a cue with a DCA cue and firing either one fires both.
3. **Type-specific section** — collapsible group, content depends
   on the cue type. For audio: file picker, gain slider, fade
   in/out, trim, pan, loop, output device, output matrix, object
   audio. For light: universe + channel grid. For video:
   geometry + opacity + screen index. For OSC: address + args.
   And so on.

If the Inspector feels cramped on a smaller screen, drag the
splitter between it and the cue list. The Inspector is also
dockable — drag its title bar out to float it on a second monitor.

### Tearing off a section

Each type-specific section (Audio, Object Audio, Fade, Light, Visual, OSC,
MIDI, MSC, Group, Wait…) can be pulled out on its own. **Grab the section's
title and drag it out.** It becomes a floating window you can drop anywhere,
including another monitor. Close that window and the section docks back
into exactly the spot it came from.

A torn-off section follows the selection like the rest of the Inspector. It
hides while you select a cue that doesn't use it, and comes back when you
select one that does.

---

## Reset buttons

Every slider and numeric field has a small **Reset** button
next to it. Click to return the value to its sensible default:

| Field | Reset value |
|---|---|
| `gainDb` | `0.0` (unity) |
| `pan` | `0.0` (centre) |
| `fadeInSeconds` / `fadeOutSeconds` | `0` |
| `trimInSeconds` / `trimOutSeconds` | `0` (no trim) |
| `opacity` | `1.0` (fully opaque) |
| Visual `posX` / `posY` | `0` |
| Visual `posW` / `posH` | `1.0` |

The gain slider also **snaps to 0 dB** within a small dead zone —
drop the handle near the centre and it lands exactly on unity
without you having to fight for it.

---

## Multi-cue editing

Select multiple cues with <kbd>Shift</kbd>-click or
<kbd>Ctrl</kbd>-click. The Inspector shows the common fields;
editing a value applies it to every cue in the selection.

Fields with conflicting values across the selection show
"`<multiple>`" — set them to the same thing to converge.

---

## Live-applied vs. on-fire

Some Inspector changes apply to a currently-playing cue
immediately:

- **gainDb** — voice gain updates in real time
- **pan** — voice pan updates in real time
- **outputGainsDb** (the output matrix) — channel gains
  update in real time

Other changes only apply on the next fire:

- **filePath** — switching the file requires reload
- **fadeInSeconds** — the fade is computed at fire time
- **trimInSeconds / trimOutSeconds** — read-out bounds set
  at fire time
- **loop** — voice loop state set at fire time
- Everything for Light / Video / OSC / MIDI — fire-time only

If you need to change a fire-time field on a currently-playing
cue, **stop the cue**, edit, **re-fire**.

---

## Undo

<kbd>Mod</kbd>+<kbd>Z</kbd> undoes the last edit in the Inspector.
<kbd>Mod</kbd>+<kbd>Y</kbd> (Windows/Linux) or
<kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> (macOS) redoes.

The undo stack is per-workspace and cleared on save/open. There's
no "redo across a save" semantic — once you save, the stack
resets.

---

## Effects rack (Audio cues only)

Audio cues have an **Effects** tab in the audio editor. The rack chains
effects per cue:

| Effect | For |
|---|---|
| Parametric EQ | 6-band tone shaping, including high- and low-pass filters |
| Compressor | Evening out levels, taming peaks |
| Reverb | Room, hall, cathedral |
| Delay | Echoes, slapback |
| Distortion *(new in 1.0.2)* | Grit and overdrive: radios, megaphones, blown speakers |
| Lo-Fi *(new in 1.0.2)* | Bit crusher and sample-rate reducer: old recordings, 8-bit games |
| Pitch Shift *(new in 1.0.2)* | Higher or deeper without changing speed: monsters, chipmunks |
| Tremolo *(new in 1.0.2)* | Volume wobble. Turn **Stereo** up for an auto-pan |

- **Presets** *(new in 1.0.2)* — **Presets ▾** loads a ready-made chain, grouped
  by Voice (Telephone, Old radio, Megaphone, Walkie-talkie, Voice
  clarity), Space (Next room, Far away, Small room, Concert hall,
  Cathedral, Slapback, Canyon echo, Stadium announcer), Character (Monster,
  Giant, Chipmunk, Robot, Ghost, Underwater, Old record, 8-bit game,
  Warble, Auto-pan) and Mix (Loud & punchy, Gentle leveller, Warm, Bright,
  Remove rumble). A preset replaces the track's effects; quewi asks first
  if there are any. **Save current effects as preset…** keeps your own
  chains, which are then listed in the same menu.
- **Add** — click `+` Add, pick a type.
- **Reorder** — drag the effect row.
- **Enable / disable** — checkbox at the top of each row.
- **Remove** — `✕` button on the row.
- **Edit parameters** — adjust each effect's sliders inline, or
  click **Edit…** to open a visual editor. The **EQ** opens a
  frequency-response curve with draggable band handles; the
  **Compressor** opens an interactive transfer curve with a live
  gain-reduction meter (drag threshold/ratio, wheel for knee).

Effects are stored with the cue and persist across save/load. Changes
reach the cue as you make them, so firing it from the cue list with the
editor still open plays the new settings.

**Live, or rendered.** A cue that plays its original file applies the rack
live. **Render to File** writes the edit, effects included, to a WAV and
switches the cue to play that file. From then on the rack isn't applied a
second time on top. Once a cue plays its render, the button reads
**Update Render** and re-renders into that same file without a save
dialog. Use **Render As…** when you want a new file. If you change
effects on a rendered cue and close the editor, quewi offers to update
the render.
