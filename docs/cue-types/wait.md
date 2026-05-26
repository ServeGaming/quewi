# Wait cue

Pauses the cue chain for a fixed duration. Use to space out
auto-continue / auto-follow sequences without ugly empty pre-
wait values.

## Inspector fields

| Field | Meaning |
|---|---|
| **Duration** | Seconds to wait |

## When to use it

- **Visible structural delay** — a noticeable hold in the show
  that should appear as its own cue in the list ("hold for
  laugh").
- **Spacing in a Group cue** — combined with the group's
  Timeline mode, a Wait cue between two children inserts a gap.

For "wait one second before the next cue fires", you can either:

- Use Wait + AutoFollow, OR
- Just put `postWait: 1` on the previous cue.

The Wait cue version is more visually obvious in the cue list,
but functionally equivalent.

## Behaviour

- GO fires the cue → the wait timer starts.
- After the duration, the cue is considered finished.
- If continue mode is AutoFollow or AutoContinue, the next cue
  fires.
- Pressing GO mid-wait skips the wait and fires the next cue
  immediately.
- Panic / Fade All / Pause stop the wait.
