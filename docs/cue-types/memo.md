# Memo cue

A cue that does nothing. It's a note in the cue list.

## Inspector fields

Just the common ones — name, number, notes, color. No type-
specific settings.

## What it's for

Memo cues are bookmarks for the operator and structural markers
for the cue list. Examples:

- **Section dividers** — "Act 2 begins", "Scene shift", "House out"
- **Operator reminders** — "Stand by for cue 5 — fast follow"
- **Holding rows** — placeholder during cue-list authoring
- **Visual separation** — quewi colours the row tint from the
  `color` field; tint-block sections of the list with coloured
  Memos.

## Behaviour

- GO fires the cue and immediately moves past it. The Memo "ran"
  — the next GO fires whatever's after.
- AutoContinue / AutoFollow on a Memo cue fires the next cue
  after the (effectively zero) duration.
- Skip-armed Memo cues are skipped entirely; GO walks past them
  without firing.

## Why have them in the cue chain at all?

A Memo with AutoContinue + a non-zero `postWait` becomes a
labelled silence in the show — "ten seconds for the audience to
get back from intermission, labelled so the operator knows
that's expected behaviour."

## OSC

The Memo cue's `type` field in OSC payloads is `"memo"`. It
doesn't accept any type-specific fields beyond the common set.
