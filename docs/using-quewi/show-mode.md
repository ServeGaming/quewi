# Show Mode

Show Mode locks the UI down so an operator focused on pressing
GO can't accidentally clobber the show by hitting a stray key
or dropping a file on the wrong window.

---

## Turning it on

Three ways:

- **Menu**: File → Show Mode
- **Keyboard**: <kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd>
- **Over OSC**: there's no dedicated address yet; remote-control
  rigs typically use a hardware key bound to the shortcut

When Show Mode flips on, an amber banner appears at the top of
the window — "SHOW MODE — editing locked" — so anyone glancing
at the screen knows the operator can only run, not edit.

---

## What gets locked

| Capability | In Edit Mode | In Show Mode |
|---|---|---|
| **GO** | ✅ | ✅ |
| **Pause / Fade All / Panic** | ✅ | ✅ |
| **Select cue (mouse, arrow keys)** | ✅ | ✅ |
| **Copy a cue** (read-only inspection) | ✅ | ✅ |
| Edit a field in the Inspector | ✅ | ❌ |
| Cut / paste / duplicate / delete | ✅ | ❌ |
| Drag to reorder cues | ✅ | ❌ |
| Drop a file on the cue list to add it | ✅ | ❌ |
| Drop a file on the window | ✅ | ❌ |
| Right-click context menu | ✅ | ❌ |
| File / Edit / Cue / List menus | ✅ | ❌ |
| New cue (single-key shortcuts) | ✅ | ❌ |
| Undo / redo | ✅ | ❌ |

The lock is genuinely enforced — not just visual. The cue list
swallows edit shortcuts, the right-click menu is suppressed,
drag-reorder is disabled (it doesn't even trigger the indicator),
external file drops are silently rejected with a status bar
nudge.

---

## Password protection

The first time you enter Show Mode, quewi asks if you want to
set a password.

- **Skip** — Show Mode toggles freely both ways. Useful for
  solo testing.
- **Set a password** (4+ characters) — required to *leave* Show
  Mode. Entering Show Mode itself remains a single keystroke,
  but exit asks for the password.

Use case: backstage. Stage manager presses GO, occasionally hits
weird key combos by accident. Without a password, a stray
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd> drops them back
into edit. With a password, that keystroke prompts for the
code; misfire = nothing happens.

The password is stored hashed (SHA-256) in QSettings. There's no
recovery — if you forget it, delete the `showMode/passwordHash`
key in your settings file:

=== "Windows"
    Registry: `HKCU\Software\ServeGaming\quewi\showMode\passwordHash`

=== "macOS"
    `~/Library/Preferences/com.ServeGaming.quewi.plist`

=== "Linux"
    `~/.config/ServeGaming/quewi.conf`

---

## When to flip it on

Best practice:

1. **Pre-flight passes** — run [pre-flight](preflight.md), green-light it.
2. **Operator is at the desk** — they know they're driving the show.
3. **Flip Show Mode on** — at fifteen minutes to house open, or
   whenever the cueing edits are settled.
4. **Run the show** — GO, GO, GO, Pause for intermission, GO, …
5. **Flip Show Mode off** when the show's over — for the post-show
   notes pass.

If you find yourself toggling Show Mode mid-show to fix a typo,
something's wrong with the cue list. Stop the show first
(<kbd>Esc</kbd>), fix it, run the relevant section of cues
in rehearsal again, then resume.

---

## Show Mode vs the "armed" flag

Both prevent accidental cue execution, but they're different
mechanisms:

- **Armed flag**: per-cue. An unarmed cue is skipped on GO. Use
  for cues that depend on a conditional ("only fire the bird-call
  cue if the kids' choir is on stage today").
- **Show Mode**: workspace-wide. Prevents *editing*, not firing.
  GO fires everything armed.

You can absolutely toggle armed flags during a show — the cue
list still selects, you can still see the inspector (read-only),
and the **Arm/Disarm** action (<kbd>A</kbd> in the cue list)
toggles the armed state without entering an editing dialog.

Wait, no — Arm/Disarm is an edit action and IS blocked in Show
Mode. If you need to disarm mid-show, you'll need to leave Show
Mode, toggle, re-enter.

---

## Limitations

- **The OSC remote API is NOT locked by Show Mode.** A controller
  with quewi's address can still send `/quewi/cue/add` or
  `/quewi/undo` while quewi-the-app is in Show Mode. The lock is
  for the operator at the local keyboard, not for the network.
  This is by design — backstage controllers running cues are
  expected to be trusted devices.
- **Auto-recovery from journal is NOT prevented.** If quewi crashes
  mid-show, the recovery dialog on next launch is editable.
  Show Mode resets on relaunch.
