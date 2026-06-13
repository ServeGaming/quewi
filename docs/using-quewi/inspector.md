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
   mode, notes, armed, color.
3. **Type-specific section** — collapsible group, content depends
   on the cue type. For audio: file picker, gain slider, fade
   in/out, trim, pan, loop, output device, output matrix, object
   audio. For light: universe + channel grid. For video:
   geometry + opacity + screen index. For OSC: address + args.
   And so on.

If the Inspector feels cramped on a smaller screen, drag the
splitter between it and the cue list. The Inspector is also
dockable — drag its title bar out to float it on a second monitor.

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

Audio cues have an **Effects** tab in the editor at the bottom
of the Inspector. The rack chains EQ, Compressor, Reverb, and
Delay effects per-cue.

- **Add** — click `+` Add, pick a type.
- **Reorder** — drag the effect row.
- **Enable / disable** — checkbox at the top of each row.
- **Remove** — `✕` button on the row.
- **Edit parameters** — adjust each effect's sliders inline, or
  click **Edit…** to open a visual editor. The **EQ** opens a
  frequency-response curve with draggable band handles; the
  **Compressor** opens an interactive transfer curve with a live
  gain-reduction meter (drag threshold/ratio, wheel for knee).

Effects are stored with the cue and persist across save/load.
They apply during playback (live, the audio you hear includes
the rack) AND during the editor's render-to-WAV export.
