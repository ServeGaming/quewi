# Quewi OSC API — Tier 1–3 changes shipped

Recap for the HeliOSC Claude Code session. All nine items from
`QUEWI_OSC_FEATURE_REQUESTS.md` are now live in quewi `v0.9.56+`.
The full reference table is in
[`docs/osc-control/reference.md`](https://github.com/ServeGaming/quewi/blob/main/docs/osc-control/reference.md);
this doc summarises what changed, what the final wire-shape ended
up as, and the few places where implementation reality differed
from the proposal.

---

## Tier 1 — shipped in v0.9.55

### 1. `/quewi/notify/cue/playback` (4 Hz playback heartbeat)

**Final shape — matches proposal:**

```
/quewi/notify/cue/playback   s s d d d
                             id  state  elapsed  remaining  position
```

- `id` — cue UUID string
- `state` — `"playing"` | `"paused"` (today). `"fading-out"` is
  reserved — the address shape doesn't change when it lands.
- `elapsed` — seconds since fire (after preWait)
- `remaining` — seconds until natural finish, or `-1.0` if unknown
  (loops, indefinite cues)
- `position` — playhead in source file, in seconds

**Behaviour:**
- Pushed at ~4 Hz (250 ms cadence) while any audio voice is alive.
- Self-starts on `GoEngine::cueFired` so the first tick lands within
  ~10 ms of fire, not up to 250 ms later.
- Self-starts on `/quewi/subscribe` if audio is already playing
  when the subscriber connects, so a freshly-subscribed remote sees
  progress immediately.
- Self-stops the next tick after voice count or subscriber count
  hits 0 — idle quewi sits at 0 % CPU when no remote is watching.

**Caveats / notes:**
- Audio cues only. Lighting and video don't surface per-cue running
  state to the server-side OSC layer yet. The `state` value will
  expand to cover those once their engines do.

---

### 2. `/quewi/query/playingCues`

**Final shape — matches proposal:**

```
Peer:  /quewi/query/playingCues
Quewi: /quewi/reply/playingCues   s   JSON [{id, type, number, state}, ...]
```

`state` values match `/notify/cue/playback`.

**Use cases this unlocks (per HeliOSC's brief):**
- Rebuilding the "now playing" view after a reconnect without
  replaying the notify stream.
- Driving Fade All against ground truth instead of accumulating
  from notify events that could have been lost.

---

### 3. `durationSeconds` in AudioCue JSON

**Final shape — matches proposal:**

```json
{
  "type": "audio",
  "filePath": "/sfx/thunder.wav",
  "durationSeconds": 4.523,
  "gainDb": 0.0
}
```

- Read-only. `fromPayload` ignores it on read-back.
- **Absent** (not zero) when the file isn't decodable yet. Remotes
  should treat absent as "unknown — show indeterminate spinner"
  rather than rendering 0 seconds.
- Same field on video cues NOT shipped yet — the video engine
  doesn't expose per-file duration on the cue today; that's a
  follow-up.

---

## Tier 2 — shipped in v0.9.56

### 4. Cue reorder endpoint

**Final shape — chose the path-style form for consistency with
`/cue/<n>/set/<field>`:**

```
/quewi/cue/<num>/move    i   new_row
```

`new_row` is 0-based and target-frame (after move): a cue at row
5 moved to `new_row=2` ends up at row 2; rows 2–4 shift down.
Numeric coercion is permissive — `i / h / f / d` all accepted.

**Notifications fired after the move applies:**

```
/quewi/notify/cue/moved             s i i   id   old_row   new_row
/quewi/notify/cueList/reordered     s s     list_id   JSON [ordered cue UUIDs]
```

Both pushed together so a remote can pick whichever shape it
needs — `moved` for incremental UI updates, `reordered` for cache
correctness.

Undoable via `/quewi/undo`.

**Caveats:**
- Only OSC-initiated moves currently emit the notifications.
  GUI-side drag-reorder doesn't push them yet — that needs a
  signal on `CueList` we haven't added. Tracking as a follow-up.
  In practice this only matters if a Quewi GUI user and an OSC
  remote are editing simultaneously; the single-operator case is
  fully covered.

---

### 5. Per-engine graceful fade

**Final shape — duration first, default sensible per engine:**

```
/quewi/lighting/fadeOut   optional f seconds (default 2.0)
/quewi/video/fadeOut      optional f seconds (default 1.0)
/quewi/fadeAll            optional f seconds (default 2.0)
```

**Implementation notes per engine:**

- **Lighting**: walks every active universe, queues a
  `fadeChannels` to 0 for every non-zero channel. Existing fade-in
  cues are superseded (fadeChannels semantics — last writer wins
  per channel).
- **Video**: animates each active layer's opacity from its current
  value to 0 in parallel, then `stopAll`s after the duration to
  release file handles + GPU resources.
- **Audio**: delegates to existing `AudioEngine::stopAll(seconds)`
  which respects each voice's per-voice gain ramp.

`/quewi/fadeAll` is the headline ask — calls all three engines'
fade-outs with the same duration in one call.

---

### 6. Workspace state in queries + notifications

**Final query shape — matches proposal:**

```
Peer:  /quewi/query/workspace
Quewi: /quewi/reply/workspace    s   JSON {name, path, dirty, lastSavedTs}
```

- `name` — workspace display name (same as `/query/showName`)
- `path` — absolute path on quewi's machine, or `""` if untitled
- `dirty` — `true` if there are unsaved changes
- `lastSavedTs` — Unix epoch seconds. Derived from filesystem mtime
  of the saved file; `0` if untitled. (Proposal called for cached
  in-memory value; mtime is cheaper and matches the user's
  intuition of "last saved" anyway.)

**Notification shape — matches proposal:**

```
/quewi/notify/workspace/dirty    T  / F
```

Wired to `Workspace::dirtyChanged`. Fires on both directions
(becomes-dirty AND becomes-clean).

The existing `/quewi/notify/workspace/changed` stays — different
semantic: full reload (file opened / new / closed) versus
incremental dirty-state transitions.

---

## Tier 3 — shipped in v0.9.56

### 7. Subscribe confirmation reply

**Final shape — matches proposal:**

```
Peer:  /quewi/subscribe   (optional s pattern)
Quewi: /quewi/reply/subscribe    s i   pattern   active_count
```

- `pattern` — the value the server actually registered (with
  default `/quewi/notify/*` substituted if the peer sent blank).
- `active_count` — total distinct (host, port, pattern) entries
  currently registered. Useful for "subscribed (3 active)" status
  in remote UIs.

Sent once per `/quewi/subscribe`. Idempotent: a duplicate subscribe
that gets deduplicated still fires a reply (the count is correct).

---

### 8. Reorder + cueList structure notifications

Shipped as part of #4 above — the two notifications fire together:

```
/quewi/notify/cue/moved             s i i
/quewi/notify/cueList/reordered     s s   list_id  JSON [ordered ids]
```

See #4 for caveat about GUI-side drag-reorder not yet emitting.

---

### 9. Cue-list metadata in `/reply/cueLists`

**Final shape — added a sibling query rather than changing the
existing one, to preserve backward compat:**

```
Peer:  /quewi/query/cueListDetails
Quewi: /quewi/reply/cueListDetails    s   JSON [{id, name, cueCount, isActive}, ...]
```

`/quewi/query/cueLists` still returns `s s s s ...` (id/name
pairs) for any remote that already consumes it.

---

## Wire-protocol summary

Total new endpoints across all three tiers:

| Address | Direction | Args |
|---|---|---|
| `/quewi/notify/cue/playback` | quewi → peer (4 Hz push) | `s s d d d` id, state, elapsed, remaining, position |
| `/quewi/query/playingCues` | peer → quewi | — |
| `/quewi/reply/playingCues` | quewi → peer | `s` JSON |
| `/quewi/cue/<num>/move` | peer → quewi | `i` new_row |
| `/quewi/notify/cue/moved` | quewi → peer (push) | `s i i` id, old, new |
| `/quewi/notify/cueList/reordered` | quewi → peer (push) | `s s` list_id, JSON |
| `/quewi/lighting/fadeOut` | peer → quewi | optional `f` seconds |
| `/quewi/video/fadeOut` | peer → quewi | optional `f` seconds |
| `/quewi/fadeAll` | peer → quewi | optional `f` seconds |
| `/quewi/query/workspace` | peer → quewi | — |
| `/quewi/reply/workspace` | quewi → peer | `s` JSON |
| `/quewi/notify/workspace/dirty` | quewi → peer (push) | `T` / `F` |
| `/quewi/reply/subscribe` | quewi → peer (now sent on every subscribe) | `s i` pattern, active_count |
| `/quewi/query/cueListDetails` | peer → quewi | — |
| `/quewi/reply/cueListDetails` | quewi → peer | `s` JSON |

Audio cue JSON gains:

| Field | Type | Notes |
|---|---|---|
| `durationSeconds` | `d` (double, seconds) | Read-only. Absent when undecodable. |

---

## Open follow-ups

These aren't blocking anything HeliOSC asked for but you might
want to know they're tracked:

- **State value `"fading-out"`** in `/notify/cue/playback`. Address
  + arg shape doesn't change when it ships; HeliOSC's defensive
  parser already accepts unknown state strings.
- **Video cue `durationSeconds`** in cue JSON. Same shape as audio
  but the video engine doesn't expose per-cue duration yet.
- **GUI-side drag-reorder** push notifications. OSC-initiated
  moves emit `moved`+`reordered` already; GUI moves don't.
- **Lighting / video state** in `/quewi/query/playingCues` results.
  Today only audio shows up. The engines need to surface per-cue
  running flags first.

If any of those matter for a HeliOSC feature, ping back and we'll
prioritise.

---

## Versions

- Tier 1 #1, #2, #3 → **v0.9.55**
- Tier 2 #4, #5, #6 + Tier 3 #7, #8, #9 → **v0.9.56**

The full address reference always lives at
[`docs/osc-control/reference.md`](https://github.com/ServeGaming/quewi/blob/main/docs/osc-control/reference.md)
— treat that as the source of truth; this doc is just the recap
of what changed.
