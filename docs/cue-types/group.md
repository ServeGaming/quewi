# Group cue

A container that fires multiple child cues with one GO. The
*mode* determines how the children are dispatched.

## Modes

| Mode | Behaviour |
|---|---|
| **FireAll** | All children fire simultaneously when the group fires. |
| **FireFirst** | The first child fires; subsequent GOs walk through the rest one at a time. |
| **Timeline** | Children fire at staggered offsets — child 1 at t=0, child 2 at t=`stepInterval`, child 3 at t=`2×stepInterval`, … |

## Inspector fields

| Field | Meaning |
|---|---|
| **Mode** | FireAll / FireFirst / Timeline |
| **Step interval** | (Timeline mode only) seconds between staggered fires |
| **Children** | Ordered list of UUIDs of cues belonging to this group |

## Use cases

- **"Lights and sound together"** — FireAll group containing a
  Light cue + an Audio cue. One operator GO press lands both.
- **Walking entrance** — Timeline group with a Light fade-up at
  t=0, footstep audio at t=0.5, dialogue audio at t=1.2.
- **Sequential effects** — FireFirst group of building effects;
  the operator paces them with GO presses.

## Authoring

Drag cues onto a Group cue in the list to make them children.
The group expands like a tree node so you can see its children
inline.

A child cue can be **fired independently** — GO directly on the
child works; the group is just a convenience.

## Limitations (v0.9.x)

- **Cue state "finished" is not emitted** for Group cues. A
  controller can't tell when all of a group's children have
  finished via push notifications; poll `/quewi/query/cue` if
  you need that signal. Coming in v1.1.
- **No Group-Random mode.** Planned post-1.0.
