# Concepts

Read this once before building a show for real. Five terms, then
you're set.

---

## Workspace

A **workspace** is everything in a single show file. One `.quewi`
file = one workspace. It contains:

- One or more cue lists
- The patch (audio outputs, DMX universes, OSC destinations, MIDI
  ports, video surfaces)
- The cart grid (optional alternate UI for SFX shows)
- The script viewer state (if the operator loaded an annotated
  script PDF)
- Undo history (cleared on save / open)

Workspaces are single-author. Quewi doesn't try to be a
multi-operator collaborative editor — for that, point a second
quewi at the same machine over OSC.

---

## Cue list

A **cue list** is an ordered list of cues. One workspace can have
multiple cue lists — typically a "Main" list for the show
top-to-bottom plus separate lists for SFX, light states, and
scene shifts. Each cue list has its own GO pointer; switching
the active list (tab strip above the cue list) switches what GO
fires.

The active cue list is the one whose next-cue indicator drives
the transport bar. The other lists keep their own pointers in
the background — switch back later and you're where you left off.

---

## Cue

A **cue** is the unit of work. Press GO, one cue fires. Every cue
has:

- A **number** (typically integer, but quewi allows decimals so
  you can insert cue `3.5` between `3` and `4` without renumbering)
- A **name** (whatever reads well — "Overture", "Doorbell", "FOH
  goes dark")
- A **type** — Audio, Light, Video, OSC, MIDI, Fade, Wait, Group,
  Memo, or one of the [control cues](../cue-types/control-cues.md)
- **Pre-wait** — pause before firing (auto-continue scenarios)
- **Post-wait** — pause before the next cue fires (chain timing)
- **Continue mode** — what happens after this cue: stop (operator
  has to press GO again), auto-continue (next cue fires after
  post-wait), or auto-follow (next cue fires immediately when
  this one finishes)
- **Notes** — free-form text the operator can read on stage
- **Armed flag** — toggle a cue off without deleting it

Audio, Video, and Light cues have whole inspectors full of
type-specific settings (file path, gain, fade times, geometry,
channels, etc.). The [cue-type reference](../using-quewi/cue-types.md)
covers each one in detail.

---

## GO

**GO** is the only button that matters during a show. Press
<kbd>Space</kbd>, the next cue fires. Quewi shows you what GO
will fire next in the transport bar — name + number + type — so
you always know what's coming.

After a cue fires, the pointer advances:

- If the cue's continue mode is **DoNotContinue**, the pointer
  moves to the next row but GO has to be pressed again.
- If **AutoContinue**, quewi fires the next cue automatically
  after the current cue's post-wait elapses.
- If **AutoFollow**, the next cue fires the moment the current
  cue finishes (audio voice ends, fade completes, wait elapses).

A skip-armed cue is treated as not present — GO walks past it
without firing.

---

## Transport actions (always live)

Four actions stay reachable even in Show Mode:

| Action | Key | What it does |
|---|---|---|
| **GO** | <kbd>Space</kbd> | Fire the next cue |
| **Pause** | <kbd>Mod</kbd>+<kbd>.</kbd> | Pause every playing audio voice (keeps read position). Resume restarts from where you paused. |
| **Fade All** | <kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>.</kbd> | Fade every running cue out over 2 seconds — clean musical ending. |
| **Panic** | <kbd>Esc</kbd> | Hard stop everything: 50 ms audio fade, lighting blackout, video stopped. Use when something's gone wrong. |

The transport bar's GO button glows green when there's a cue
queued, blue when there isn't (end of list or nothing armed).

---

## Pre-flight

**Pre-flight** is a one-click validator. Run it before the house
opens. It checks:

- Every audio cue's file actually exists on disk
- Every video / image / text cue has a valid file or text
- Every OSC cue's host resolves
- Every DMX universe is patched to an output
- Every targeting cue points at a real cue
- Pre/post-waits aren't suspiciously huge (60 s+ warning)

Result is a single dialog: green "Ready" if nothing tripped, or
an itemised list of problems with the cue number next to each.

---

## Show Mode

**Show Mode** locks the UI down to "operator pressing GO" mode.
While on:

- Edit / Cue / List / File menus are disabled
- The Inspector is disabled
- The cue list rejects drops, drag-reorders, cut/paste/delete,
  and right-click menus
- A red banner appears at the top of the window so everyone knows

GO, Pause, Fade All, and Panic stay live. So does cue selection
(arrow keys) so you can scroll the list to see what's coming.

Toggle with <kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd>. You
can set a password the first time you enable it — that way an
accidental shortcut press mid-show doesn't drop you back into
Edit Mode.

[Show Mode details →](../using-quewi/show-mode.md)

---

## Patch

The **patch** is how cues reach the outside world. It holds:

- **Audio outputs** — named device IDs ("FOH", "Lobby") that
  Audio cues can target via the per-cue output matrix
- **Speaker arrays** — for object-audio routing via VBAP
- **DMX universes** — which Art-Net / sACN / DMX-USB output owns
  each universe
- **OSC destinations** — reusable host:port targets so OSC cues
  don't have to re-type the address each time
- **MIDI ports** — named output ports for MIDI / MSC cues
- **Projection surfaces** — screen ↔ display mapping for Video

You'll spend most of your time NOT touching the patch — quewi
auto-creates sensible defaults. You'll come back to it when a
new piece of hardware appears or the show grows past one output
zone.

---

## OSC remote control

Quewi is OSC-controllable end-to-end. From another app (an iPad
remote, a Stream Deck, your own controller code) you can:

- Trigger GO / Panic / Pause / Fade All
- Fire a specific cue by number
- Edit any field of any cue live
- Query the cue list as JSON
- Subscribe to live push notifications (cue added/changed/removed/
  fired/finished)

The full address surface is in [OSC remote control](../osc-control/reference.md).

---

## File format

`.quewi` files are SQLite databases. Open one in any SQLite
browser to inspect cues, patches, scripts. The format is
versioned and forward-compatible. Details:
[reference / file format](../reference/file-format.md).

---

## Next steps

- [Cue types overview](../using-quewi/cue-types.md) — every cue
  type with what it's for.
- [Transport in depth](../using-quewi/transport.md) — pre-wait /
  post-wait / continue modes / chain timing.
- [OSC reference](../osc-control/reference.md) — drive quewi
  from outside.
