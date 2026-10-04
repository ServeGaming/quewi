# Keyboard Shortcuts

Checked against the actual wiring in `src/app/MainWindow.cpp` for 1.0.1.

`Mod` = `Ctrl` on Windows/Linux, `Cmd` on macOS.

!!! note "Changed in 1.0.1"
    **New MSC cue moved to `Mod + Alt + M`.** It used to share
    `Mod + Shift + M` with *View → Mix (DCA) grid*, and two actions on one
    key cancel each other out — so neither worked.

---

## Transport — always live, even in Show Mode

| Action | Default |
|---|---|
| GO (fire next cue) | `Space` |
| Panic — hard stop everything | `Esc` |
| Pause / Resume (press again to resume) | `Mod + .` |
| Fade All — fade sound, video and lights out over 2 s | `Mod + Shift + .` |

These four are rebindable in **Tools → Keyboard shortcuts…** — useful for a
footswitch or a Stream Deck that sends a particular key.

---

## File / show

| Action | Default |
|---|---|
| New show | `Mod + N` |
| Open… | `Mod + O` |
| Save | `Mod + S` |
| Save As… | platform default (`Mod + Shift + S` on macOS; not bound on Windows, where `Mod + Shift + S` opens the script follower) |
| Close show | `Mod + W` |
| Quit | `Mod + Q` (mac) / `Alt + F4` (Win) |

---

## Edit

| Action | Default |
|---|---|
| Undo | `Mod + Z` |
| Redo | `Mod + Y` (Win/Linux) · `Mod + Shift + Z` (mac) |
| Find / replace | `Mod + F` |

---

## Command menu

| Action | Default |
|---|---|
| Command menu — search | `Mod + K` |
| Command menu — key menu (the *leader*: tap it) | `` ` `` (backtick) |
| Jump straight to a category, or run a pinned action | hold `` ` `` + letter |

In the panel: `↑` `↓` move, `↵` runs (`Mod + ↵` opens a cue in its editor),
`Ctrl + P` pins to a chord, `Backspace` goes up a level, `Esc` closes. The
leader and `Mod + K` are both rebindable; the leader is also set in
**Preferences → Command menu**. In a text field the leader just types.
See [Command menu](command-menu.md).

---

## Cue creation — cue list focus only

Bare letter keys so they're fast while writing cues. They only work while the
cue list has focus, so typing on the Soundboard or Mix page — or in a text
field — never creates cues in a list you can't see.

| New cue type | Key |
|---|---|
| Memo | `M` |
| OSC | `O` |
| Audio | `A` |
| Fade | `F` |
| Light | `L` |
| Light Fade | `Shift + L` |
| Video | `V` |
| Image | `I` |
| Text | `T` |
| Wait | `W` |
| Start | `Shift + S` |
| Stop | `Shift + X` |
| Goto | `Shift + G` |
| Group | `Mod + G` |
| MIDI | `Shift + M` |
| MSC | `Mod + Alt + M` |
| Toggle armed | `E` |
| Delete | `Del` |

Import from URL (`Mod + U`) works from anywhere.

---

## Cue list — selection + clipboard

| Action | Default |
|---|---|
| Previous / next cue | `↑` / `↓` |
| First / last cue | `Home` / `End` |
| Page up / down | `PgUp` / `PgDn` |
| Copy / cut / paste | `Mod + C` / `X` / `V` |
| Duplicate selection | `Mod + D` |
| Delete selection | `Del` / `Backspace` |

Copy still works in Show Mode (read-only inspection). Cut, paste,
duplicate, and delete are blocked in Show Mode.

---

## Tools / windows

| Action | Default |
|---|---|
| Pre-flight | `Mod + P` |
| OSC Monitor | `Mod + 1` |
| Patch editor | `Mod + Shift + P` |
| Script follower | `Mod + Shift + S` |
| Show Mode toggle | `Mod + Shift + L` |
| Inspector panel | `Mod + I` |
| Soundboard | `Mod + Shift + C` |
| Mix (DCA) grid | `Mod + Shift + M` |
| About quewi | `Mod + ?` |

---

## Soundboard pads

Each pad can have its own key — right-click the pad → **Set keybind…**. See
[Soundboard](soundboard.md#keybinds) for where those keys work (board only,
anywhere in quewi, or system-wide).

---

## In the editors

In the audio editor and the video editor, **Space plays and pauses the
editor's preview** rather than firing GO, wherever the focus is in that
window (except while you're typing in a text box, such as a trigger's name). Close the editor (or click the main window) to use Space as GO
again.

- Video editor keys (In / Out, Select and Razor tools, Delete to cut a
  section…): see [The video editor](../cue-types/video.md#keys).
- Audio editor's Lighting tab: **M** adds a trigger at the playhead, **T**
  taps the tempo, **Ctrl+G** groups the selected triggers, **Delete**
  removes them, and **Alt** while dragging in the lighting lane places a
  trigger without snapping to the beat (Alt+click picks one trigger out of
  a group). See [Lighting triggers](lighting-triggers.md#editing-several-at-once).

---

## Bigger text in the cue list

Hold <kbd>Ctrl</kbd> and scroll over the cue list to make its text (and its
rows) bigger or smaller, from 70 % to 300 %. <kbd>Ctrl</kbd>+<kbd>=</kbd> and
<kbd>Ctrl</kbd>+<kbd>-</kbd> do the same, and <kbd>Ctrl</kbd>+<kbd>0</kbd> puts it
back to normal. quewi remembers the size on this computer.

## Safe key

**Preferences → Show Mode → Safe key** can make GO and Delete need a key held
down, so a stray press does nothing. Pick **Shift**, **Ctrl**, **Alt** or any
other key (a spare key, or a foot switch that sends one), then tick **Hold the
safe key to GO** and/or **Hold the safe key to delete cues**.

- With Shift as the safe key, **Shift+Space** is GO and Shift+click on a GO
  button works; Space on its own does nothing and the status bar says
  "Hold Shift to GO".
- It covers GO from this computer: Space, the GO buttons and Show Mode. GO
  from a remote (OSC, HeliOSC) isn't affected.

