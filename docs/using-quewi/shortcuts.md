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
| Command palette | `Mod + K` |

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
