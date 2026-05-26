# Transport — GO, Panic, Pause, Fade All

The four buttons at the bottom of the window are the only controls
that stay live in [Show Mode](show-mode.md). They're the operator's
hands during a run.

---

## GO

**Action:** fires the next cue in the active list.

**Default key:** <kbd>Space</kbd>.

The transport bar's left side shows what GO will fire next —
cue number, name, type — in large legible text. The GO button on
the right is green when there's a cue queued, blue when the
pointer's past the end of the list.

After firing:

- **DoNotContinue** (default) — pointer moves to the next row,
  but GO has to be pressed again.
- **AutoContinue** — quewi fires the next cue after the
  current cue's `postWait` elapses.
- **AutoFollow** — next cue fires the moment the current cue
  finishes (audio voice ends, fade completes, etc.).

Continue mode is per-cue, edit it in the Inspector or set
defaults in Preferences.

---

## Pause

**Action:** pauses every currently-playing audio voice in place.
Fade and Wait cues mid-run continue (those are timeline-based,
not voice-based).

**Default key:** <kbd>Mod</kbd>+<kbd>.</kbd>

The voice keeps its read position. Press GO or use the OSC
`/quewi/resume` command to restart from where you paused.

Pause is reversible. Compare:

| Pause | Fade All | Panic |
|---|---|---|
| Instant silence, no fade | 2-second fade-out | 50 ms fade-out |
| Resumable | Resumable (the cue's still on the active list) | Not resumable |
| For "we're holding for a sec" | For "wrap this section cleanly" | For "something is wrong, STOP" |

---

## Fade All

**Action:** fade every running cue out over 2 seconds. Audio
fades to silence, video alpha fades to 0, lights fade to black.

**Default key:** <kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>.</kbd>

Use this when you need a clean musical ending and Panic would be
abrupt.

---

## Panic

**Action:** hard stop everything immediately.

- Audio: 50 ms fade-out (just enough to avoid a click), then voices end
- Lighting: blackout — every patched universe drops to zero
- Video: every active video voice stops

**Default key:** <kbd>Esc</kbd>.

Panic is the "something is wrong" button. The fade is 50 ms (not
0) specifically to avoid a click on the speakers — that's the
shortest fade that's smooth.

---

## Selection vs the queue pointer

These are not the same thing.

- **Selection** = the highlighted row(s) in the cue list. Click
  to select. Arrow keys move the selection. The Inspector edits
  the selected cue.
- **Queue pointer** = the cue GO will fire next. Shown in the
  transport bar. Advances automatically after each fire.

When you click a cue in the list, **both** the selection AND
the queue pointer move there. That's the most intuitive thing
the operator wants ("I want to skip to this cue now").

When the queue pointer advances after a fire, the selection
doesn't follow by default — so the operator can look ahead in
the list while the show plays. You can change this in
Preferences → Transport → **Selection follows queue pointer**.

---

## Targeting cues vs transport

Don't confuse Panic (transport action) with a Stop cue
(content). They serve different purposes:

- **Panic** kills everything in flight, no cue authoring needed.
  It's the seatbelt.
- **Stop cue** (a [control cue](../cue-types/control-cues.md))
  fires from inside the cue list, stops one specific target
  cue. Authored ahead of time; part of the show structure.

Same shape for Pause / Resume — there are control cue versions
of these for show-authored use, AND transport-bar buttons for
the operator's-fingers use.

---

## Defaults and rebinding

The transport keys are listed in [keyboard shortcuts](shortcuts.md).
Rebind any of them in **Preferences → Shortcuts** — useful if
you want a hardware footswitch sending a particular key, or to
align with another tool's muscle memory.

---

## Over OSC

Every transport action has an OSC equivalent:

| Action | OSC address |
|---|---|
| GO | `/quewi/go` |
| Pause | `/quewi/pause` |
| Resume | `/quewi/resume` |
| Fade All | (use `/quewi/cue/*/set/gainDb` for the audio; full Fade All isn't an OSC verb yet) |
| Panic | `/quewi/panic` |

Full address surface in [OSC reference](../osc-control/reference.md).
