# Keyboard Shortcuts

Audited against the actual wiring in `src/app/MainWindow.cpp`. Every
shortcut here is rebindable through **Preferences → Shortcuts** —
the defaults below are what ships out of the box.

`Mod` = `Ctrl` on Windows/Linux, `Cmd` on macOS.

---

## Transport — always live, even in Show Mode

| Action | Default |
|---|---|
| GO (fire next cue) | `Space` |
| Panic — hard stop everything | `Esc` |
| Pause all (real pause, not stop) | `Mod + .` |
| Fade All — fade running cues out over 2 s | `Mod + Shift + .` |

The transport actions register through `ShortcutManager` so a remote
console / Stream Deck can rebind them without recompiling.

---

## File / show

| Action | Default |
|---|---|
| New show | `Mod + N` |
| Open… | `Mod + O` |
| Save | `Mod + S` |
| Save As… | `Mod + Shift + S` |
| Close show | `Mod + W` |
| Quit | `Mod + Q` (mac) / `Alt + F4` (Win) |

---

## Edit

| Action | Default |
|---|---|
| Undo | `Mod + Z` |
| Redo | `Mod + Y` (Win/Linux) · `Mod + Shift + Z` (mac) |
| Find in cue list | `Mod + F` |
| Find / replace | `Mod + Shift + R` |
| Command palette | `Mod + K` |
| Preferences | `Mod + ,` |

---

## Cue creation — single-key, list focus only

These are bare letter keys so they work fast during cue-writing. The
key is consumed by the cue list, so they don't fire while a text
edit is focused.

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
| MSC | `Mod + Shift + M` |

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

## Tools / Windows

| Action | Default |
|---|---|
| Pre-flight | `Mod + Shift + P` |
| OSC Monitor | `Mod + 1` |
| Notifications | `Mod + ?` |
| Inspector toggle | `Mod + I` |
| Cart view toggle | `Mod + Shift + C` |
| Print show summary | `Mod + P` |
| Show Mode toggle | `Mod + Shift + L` |

---

## Rebinding

Open **Preferences → Shortcuts**. The list is grouped by category;
double-click a row to capture a new chord. Conflicts (two actions on
the same chord) are flagged inline. `Reset to default` per action and
`Reset all` at the bottom undo your changes.

Custom bindings are stored in QSettings under `shortcuts/<actionId>`.
They survive across updates.
