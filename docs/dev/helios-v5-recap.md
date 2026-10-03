# Quewi OSC API — v5 changes shipped

Recap for the HeliOSC Claude Code session. Everything here is on `main`
and ships in quewi **1.0.4** (commits `866c74e` for the API, `995caec` /
`0cce4a1` / `536eefe` for lighting triggers themselves, `74705f6` for video
sound and conversion, `f58c347` for soundboard → mic). The handlers live in
`src/app/MainWindowOscV5.cpp`. The full reference table is in
[`docs/osc-control/reference.md`](https://github.com/ServeGaming/quewi/blob/main/docs/osc-control/reference.md#lighting-triggers-v5);
this doc summarises what's new, the exact wire shapes, and what to watch
for.

---

## 0. Read this first: the subscription fix

**If HeliOSC subscribes with the default pattern, it has never received a
two-level notification before 1.0.4.**

`/quewi/subscribe` with no argument registers `/quewi/notify/*`. Quewi
matched that with plain OSC rules, where `*` stops at `/`. Every notify
address has at least two levels after `/quewi/notify/`
(`/quewi/notify/cue/state`, `/quewi/notify/workspace/dirty`, …), so the
default subscription matched **nothing**. Same for an explicit
`/quewi/notify/*`.

**Fixed:** a subscription pattern ending in `/*` now also matches every
deeper address. So `/quewi/notify/*` gets everything, and
`/quewi/notify/cue/*` gets `cue/state`, `cue/changed`, `cue/playback` and so
on. Patterns that don't end in `/*` still use plain OSC matching.

What to do in HeliOSC:

- If you worked around it (explicit per-address subscriptions, or a
  pattern like `/quewi/notify/cue/*`, which used to match only exactly one
  level), it keeps working. You may now get **duplicates** if you hold
  overlapping subscriptions (for example the default plus explicit ones):
  quewi sends once per matching subscription. Unsubscribe the extras or
  de-dup on receipt.
- If you used the default and assumed notifications were flaky, they
  weren't: they were never sent. Expect a lot more traffic now, including
  the 4 Hz `cue/playback` heartbeat while audio plays.
- Against an older quewi (≤ 1.0.3) the default still gets nothing.
  Check `/quewi/query/version` and fall back to explicit patterns there.

---

## 1. Lighting triggers

Points and ranges on an audio cue's song that send OSC / MIDI / MSC to a
lighting desk, or fire a cue, as the song plays past them. On a **video
cue** the same addresses reach its soundtrack's triggers, whether or not
the video's sound is on, so HeliOSC doesn't need to care which type it is.
Other cue types are ignored (no reply).

### Addresses

```
/quewi/cue/<num>/triggers/list                     → /quewi/reply/cue/triggers  s JSON
/quewi/cue/<num>/triggers/set    s JSON-array        replace all (""/[] clears)
/quewi/cue/<num>/triggers/add    f start [f end] [s name]
                                 or s JSON-object  → /quewi/reply/cue/triggers/added  s id, i index
/quewi/cue/<num>/triggers/clear
/quewi/cue/<num>/trigger/<ref>/set/<field>  <value>
/quewi/cue/<num>/trigger/<ref>/remove
/quewi/cue/<num>/trigger/<ref>/test  [s "exit"]
/quewi/triggers/armed  [T/F | i]                  → /quewi/reply/triggers/armed  T/F
/quewi/query/triggers/armed                       → /quewi/reply/triggers/armed  T/F
```

- `<num>` — cue number in the active list (any numeric type works in
  the path, as with the other `/cue/<num>/…` verbs).
- `<ref>` — **0-based index**, trigger **id** (UUID, braces optional), or
  **name** (case-insensitive). A ref that parses as an integer is always
  an index. Names must be legal in an OSC address (no spaces etc.), so
  prefer ids.
- Triggers are stored **sorted by start**. Editing `start` (or adding)
  can renumber them; `triggers/add` replies with the index it landed at.
- `test` sends the enter action (or exit with `"exit"`) right now. It
  ignores the arm switch and does **not** push `trigger/fired`.
- `triggers/armed` with no argument changes nothing and just replies.
  The switch is per computer (QSettings `triggers/armed`, default on),
  shared with **Tools → Lighting Triggers Armed**.
- Every edit is one undo step (wrapped so quewi's field-edit merging
  can't fold several remote edits into one), and pushes
  `/quewi/notify/cue/changed` like any edit. Note that the generic
  `/quewi/cue/<num>/set/lightTriggers` *does* merge consecutive edits;
  use the trigger addresses instead.
- Fire-and-forget: edits that fail (unknown cue, bad ref, bad JSON,
  unknown field) are silently ignored. Re-`list` to confirm.

### Trigger fields (`/trigger/<ref>/set/<field>`)

| Field | Value |
|---|---|
| `name` | `s` |
| `start` | `f` seconds in the song **file** (not the cue; trims don't move it), clamped ≥ 0 |
| `end` | `f` seconds; > `start` = range, otherwise (e.g. `-1`) a point |
| `enabled` | `T`/`F` |
| `enter`, `exit` | `s` whole action as JSON |
| `enter.<f>`, `exit.<f>` | one action field |

### Action fields

| `kind` | fields |
|---|---|
| `none` | — |
| `osc` | `host` s, `port` i (default 8000), `transport` i (0 UDP, 1 TCP/SLIP, 2 WS), `address` s, `args` s (comma-separated, auto-typed like an OSC cue) |
| `midi` | `midiPort` s ("" = first), `midiType` (`noteOn` `noteOff` `cc` `program` `raw`), `channel` 1–16, `data1` 0–127, `data2` 0–127, `rawHex` s (only for `raw`) |
| `msc` | `midiPort` s, `deviceId` 0–127 (127 all-call), `commandFormat` (1 Lighting, 2 Moving lights, 127 All), `command` (1 GO, 2 STOP, 3 RESUME, …, 0x0B GO OFF), `qNumber` s, `qList` s |
| `cue` | `cueId` s (UUID). Can't fire its own song (refused) |

Changing `kind` keeps the other fields, so toggling back restores them.

### Stored JSON (what `set` / `add` take)

```json
{
  "id": "6f1c2d1e-3a7b-4c55-9b0e-2f7d8a1c9e40",
  "name": "Chorus",
  "start": 42.5,
  "end": 58.0,
  "enabled": true,
  "enter": { "kind": "osc", "host": "10.101.100.101", "port": 8000,
             "transport": 0, "address": "/eos/sub/1", "args": "1.0" },
  "exit":  { "kind": "osc", "host": "10.101.100.101", "port": 8000,
             "transport": 0, "address": "/eos/sub/1", "args": "0.0" }
}
```

A point has no `end` / `exit`. Each action only carries `kind` plus that
kind's fields. Missing fields take defaults. `add` with a JSON object
always gets a fresh `id`.

### List reply

```json
{ "cue": 3.0, "id": "2b0f…", "type": "audio", "armed": true,
  "triggers": [
    { "id": "6f1c…", "name": "Chorus", "start": 42.5, "end": 58.0, "enabled": true,
      "index": 0, "range": true,
      "enter": { "kind": "osc", …, "summary": "OSC /eos/sub/1 1.0 → 10.101.100.101:8000" },
      "exit":  { "kind": "osc", …, "summary": "OSC /eos/sub/1 0.0 → 10.101.100.101:8000" } },
    { "id": "a913…", "name": "Blackout", "start": 61.0, "enabled": true,
      "index": 1, "range": false,
      "enter": { "kind": "cue", "cueId": "c4d2…", "summary": "fire cue", "cueNumber": 12.0 } } ] }
```

Extra over the stored shape: `index`, `range` per trigger; `summary` per
action (ready to show); `cueNumber` on `cue` actions whose target exists.
`type` is `"audio"` or `"video"`.

**Id format:** cue ids (`trigger/fired`, the list reply's `id`) are
**braced** like everywhere else in the API. Trigger ids and an action's
`cueId` are written **without braces**; anything you send may use either.

### Notifications

```
/quewi/notify/trigger/fired    s d s s s   cueId  cueNumber  triggerId  name  "enter"|"exit"
/quewi/notify/triggers/armed   T/F
```

- `trigger/fired` is pushed when a trigger actually sends during
  playback. Not when disarmed, not for an edge set to `none`, not for
  `test`, not for the audio editor's "Send while previewing". Points say
  `"enter"`.
- The cue is the trigger's owner: the video cue for a soundtrack's
  triggers.

### Behaviour notes

- Seeking skips points in between but catches ranges up (enter the one
  you land in, exit the one you left). Loops replay. Pause holds. Stop,
  fade-out end or natural end sends the exits of ranges you're inside.
  Panic sends nothing more, not even exits.
- A video with sound plays its triggers off the soundtrack; a silent
  video follows the picture clock.
- Timing: quewi checks every 10 ms on its UI thread, typically within
  ~15 ms of the mark. Sends land ~50 ms *before* the sound is heard (it
  tracks the engine's read position). Measured over UDP: marks at
  0.5 / 1.0 / 2.0 s reached a desk at 0.45 / 0.96 / 1.95 s. The first play
  of a freshly loaded video can be ~0.3 s late.
- A cue that had **no** triggers when it started won't pick up triggers
  added while it plays until its next GO.

---

## 2. Video ↔ audio conversion

```
/quewi/cue/<num>/convert
```

Video cue → audio cue (keeps the soundtrack); audio cue that came from a
video, or plays a video file, → video cue. Keeps id, number, name, waits,
notes, colour and link, so targets still resolve. Converting back restores
screen / geometry / opacity (stored in the audio cue's read-only
`videoOrigin`). Sound edits, effects and lighting triggers carry over.
Stops the cue first if playing. Undoable. Ignored in Show Mode and for
audio cues unrelated to video.

The swap is a remove + insert in the same row, so you'll see
`/quewi/notify/cue/removed` then `/quewi/notify/cue/added` **with the same
cue id**. Re-query the cue; its `type` has flipped.

### Video cue fields (new)

| Field | Notes |
|---|---|
| `soundEnabled` | `T`/`F`. On for new video cues, off for shows saved before 1.0.4 |
| `sound.<field>` | Any AudioCue field of the soundtrack: `sound.gainDb`, `sound.pan`, `sound.fadeInSeconds`, `sound.lightTriggers`, … |
| `"sound"` in cue JSON | Nested object with the soundtrack's AudioCue fields |

The soundtrack's `filePath` and `loop` follow the video's.

### Existing verbs that now reach video cues

- `/quewi/cue/<num>/level`, `/pan` ride the soundtrack; `/seek` moves the
  picture **and** the soundtrack (picture only when the sound is off).
- `/quewi/cue/<num>/fx/<type>/<param>` and `/fx/list` edit and report the
  soundtrack's effects.

### Also fixed

- `/quewi/cue/<num>/set/filePath` now starts decoding immediately, so the
  next GO plays. Before, the first GO after a remote file change found it
  "still decoding" and was silent.

---

## 3. Soundboard → mic

```
/quewi/soundboard/mic/device       s   output name or id ("" = off)
/quewi/soundboard/mic/input        s   input name or id ("" = none)
/quewi/soundboard/mic/gain         f   dB (sound level in mic)
/quewi/soundboard/mic/inputGain    f   dB (voice level)
/quewi/soundboard/mic/monitor      T/F | i   also play on the board's output
/quewi/soundboard/mic/passthrough  T/F | i   mix the real mic in
/quewi/query/soundboard/mic        → /quewi/reply/soundboard/mic  s JSON
/quewi/notify/soundboard/mic/changed   (no args) — any change, local or remote
```

Device lookup: exact id, then exact name, then name substring
(case-insensitive). No match → status-bar message on quewi, nothing
changes, no reply.

```json
{ "enabled": true, "device": "…", "deviceName": "CABLE Input (VB-Audio Virtual Cable)",
  "monitor": true, "gainDb": 0.0, "passthrough": true,
  "input": "…", "inputName": "Microphone (USB Audio)", "inputGainDb": 0.0,
  "outputs": [ { "id": "…", "name": "…", "virtual": true }, … ],
  "inputs":  [ { "id": "…", "name": "…", "virtual": false }, … ] }
```

Settings are per quewi computer, not per show.

---

## 4. Doc correction: `/quewi/workspace/new`

The reference used to say it was promptless. It isn't: with unsaved
changes, quewi shows **Save / Discard / Cancel on the quewi machine** and
waits; a remote can't answer it. Check `dirty` via
`/quewi/query/workspace` and `/quewi/workspace/save` first if you need it
unattended. `/quewi/workspace/open <path>` now prompts the same way (it used to load
without asking and lose unsaved changes).

---

## Suggested HeliOSC UI

- **Per-cue trigger list.** On an audio or video cue's detail view, call
  `triggers/list` and show rows of name, start (and end for ranges), an
  enabled toggle, and the `summary` strings. Refresh on
  `notify/cue/changed` for that cue. Long-press or swipe for edit /
  remove; a small form per action kind using the field tables above.
- **Arm toggle.** A prominent armed / disarmed switch in the header
  (`/quewi/triggers/armed T|F`), kept in sync from
  `notify/triggers/armed` and seeded with `query/triggers/armed` on
  connect. Make the disarmed state obvious; it's the "rehearse without the
  desk" mode.
- **Test buttons.** One per trigger (and an "exit" one for ranges) sending
  `trigger/<id>/test`. Use ids, not indices, so a reorder can't test the
  wrong one.
- **Fired flash.** On `notify/trigger/fired`, flash the matching row (by
  trigger id) and, if you show a progress bar from `cue/playback`, drop a
  tick on it. A rolling "last fired" line (name, enter/exit, time) helps
  the LD confirm the desk got it.
- **Markers on the progress bar.** Draw trigger marks on the cue's
  playback bar using `start`/`end` against `durationSeconds`. Times are
  file seconds, which is the same clock as `cue/playback`'s `position`.
- **Convert button** on video and video-derived audio cues
  (`/cue/<num>/convert`), handling the removed + added pair.
- **Mic panel** on the soundboard page from `query/soundboard/mic`, with
  pickers from `outputs` / `inputs` (sort `virtual: true` first), refreshed
  on `notify/soundboard/mic/changed`.

The full address reference always lives at
[`docs/osc-control/reference.md`](https://github.com/ServeGaming/quewi/blob/main/docs/osc-control/reference.md)
— treat that as the source of truth; this doc is just the recap of what
changed.
