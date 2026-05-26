# Pre-flight

Pre-flight is a one-click validator. Run it after building a
show and before opening the house. If it's all green, you're
ready.

Open it from **Tools → Pre-flight** or
<kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>.

---

## What pre-flight checks

The dialog walks every cue in every cue list and verifies the
common failure modes:

| Cue type | Check | Severity |
|---|---|---|
| **Audio** | File path exists on disk | Error |
| **Audio** | File can be decoded by Qt Multimedia / FFmpeg | Error |
| **Audio** | Output device exists / is connected | Error |
| **Audio** | Speaker patch (if object-audio enabled) is non-empty | Error |
| **Video / Image** | File path exists on disk | Error |
| **Video** | Screen index < number of attached displays | Warning |
| **Light** | Universe is patched to an output | Error |
| **Light Fade** | Target Light Cue exists | Error |
| **OSC** | Host resolves (DNS lookup; doesn't actually ping) | Warning |
| **OSC** | Port is in valid range (1..65535) | Error |
| **MIDI / MSC** | Port name matches an attached device | Warning |
| **Targeting cues** | `targetId` points at a real cue | Error |
| **Group** | Has at least one child | Warning |
| Any cue | `preWait` > 60 s (possible typo) | Warning |

**Errors** stop the show from going. **Warnings** don't — they're
"look at this before you run, but it might be intentional"
(e.g. a 90-second pre-wait on the intermission cue).

---

## Reading the results

Top of the dialog: a summary line.

- **✓ Ready — no problems found** in green → you're good.
- **N problems · M warnings** in red/amber → itemised list below.

Each row in the list shows:

- **Severity icon** (red dot = error, amber triangle = warning)
- **Cue location** — `[Main / Cue 4.5]` so you can find it
- **Description** — "missing file: S:/sfx/missing.wav"

Double-click a row to jump to that cue in the list. Fix the issue,
press **Re-check** at the bottom to revalidate.

---

## Tips for keeping pre-flight green

- **Don't reference files on removable drives** unless the drive
  is mounted at show time. A USB stick that wasn't plugged in
  when you built the show will fail pre-flight on the show
  laptop later.
- **Use relative paths** when possible. Quewi resolves files
  relative to the workspace's directory; this survives moving
  the show folder between machines.
- **Patch first, then build cues.** A Light cue authored before
  its universe is patched fails pre-flight until you patch
  the universe.
- **Run pre-flight after every edit session.** Catches the cue
  you renamed the file for but forgot to update.

---

## Pre-flight ≠ tech rehearsal

Pre-flight checks that the *static* structure of the show is
sound. It doesn't check:

- Whether the audio levels are right
- Whether the lighting looks like you wanted
- Whether the OSC receiver is actually expecting these messages
- Whether the operator knows the show

That's what a tech rehearsal is for. Pre-flight is the "no
typos" check, not the "does this run" check.
