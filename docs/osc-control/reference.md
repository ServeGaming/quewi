# quewi OSC Remote API

A reference for building any external app (lighting console, iPad
remote, Stream Deck plugin, Companion module, your own custom code)
that talks to quewi over OSC.

This document covers the full surface: every address quewi responds to,
every notification it pushes, and every field on every cue type that's
editable over the wire.

---

## TL;DR

| Setting | Value |
|---|---|
| Host | the machine running quewi |
| Port | **53535** (UDP) |
| Protocol | OSC 1.1 |
| Direction | bidirectional — peer sends, quewi replies on the same port and pushes live updates back |

Send `/quewi/heartbeat` and you're connected. Send `/quewi/go` and quewi fires its next cue.

```python
# Smallest working example
from pythonosc.udp_client import SimpleUDPClient
client = SimpleUDPClient("127.0.0.1", 53535)
client.send_message("/quewi/go", [])
```

---

## Four things you can do

1. **Trigger** — one-shot commands like `/quewi/go`, `/quewi/cue/start 3.5`, `/quewi/undo`. No reply expected.
2. **Edit** — mutate any field of any cue (`/quewi/cue/<n>/set/<field>`), add or remove cues, switch the active list, open/save the workspace.
3. **Query** — ask for state and get a single reply back: version, show name, list of cue lists, full cue list as JSON, single cue as JSON.
4. **Subscribe** — send `/quewi/subscribe` once and receive live push notifications whenever something changes (cue inserted, edited, removed, fired, list switched). This is how you mirror state without polling.

All four share the same UDP socket. Quewi reads the source port of every inbound message and replies / pushes back to that port, so your app only needs one socket open.

---

## Trigger reference (peer → quewi, no reply)

### Transport

| Address | Args | Effect |
|---|---|---|
| `/quewi/go` | — | Fire the next cue (same as pressing GO) |
| `/quewi/panic` | — | Hard-stop everything: 50 ms audio fade + lighting blackout + all video stopped |
| `/quewi/stop` | — | Same as `/quewi/panic` |
| `/quewi/pause` | — | Real pause — every playing audio voice freezes at its current position |
| `/quewi/resume` | — | Resume every paused voice |
| `/quewi/heartbeat` | — | No-op, useful as a keepalive ping |

### Per-engine stops

These let a controller stop one engine without affecting the others. Use `/quewi/panic` if you want everything.

| Address | Args | Effect |
|---|---|---|
| `/quewi/lighting/blackout` | — | Blackout all DMX universes (instant) |
| `/quewi/lighting/fadeOut` | optional `f` seconds (default 2.0) | Soft blackout — fades every active channel on every active universe down to 0 over the duration. |
| `/quewi/video/stop` | — | Stop all video surfaces instantly (audio keeps going) |
| `/quewi/video/fadeOut` | optional `f` seconds (default 1.0) | Soft stop — animates every active layer's opacity to 0 over the duration, then stops them. |
| `/quewi/fadeAll` | optional `f` seconds (default 2.0) | **Headline one-button graceful stop.** Audio, lighting, and video all ramp to silent / black / empty in the same window. |

### Cue navigation

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/select` | `f` cue number | Move the selection to that cue |
| `/quewi/cue/start` | `f` cue number | Fire that cue directly |
| `/quewi/cue/stop` | `f` cue number | Stop the audio voice for that cue (audio only) |
| `/quewi/select/next` | — | Move the selection **down** one cue — does NOT fire. The next `/quewi/go` targets it. |
| `/quewi/select/previous` | — | Move the selection **up** one cue — does NOT fire. |
| `/quewi/reset` | — | Return the playhead to the **top** of the active list (selects the first cue). The QLab-style "reset to top" before a run. |

Cue numbers accept any numeric type (`i / f / h / d`). Use whatever your client emits.

### Live mix control (on a *playing* cue)

Adjust a currently-playing audio cue's voice in real time, addressed by cue
number. Each is a no-op if the cue isn't playing (no live voice), so they're
safe to fire blind from a fader bank. Changes are applied immediately with no
fade — perfect for a remote fader, trackpad, or rotary.

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/<num>/level` | `f` dB | Set the live output gain of the cue's voice (e.g. `-6.0`). |
| `/quewi/cue/<num>/pan` | `f` -1.0 … +1.0 | Set the live stereo pan (L `-1` … `+1` R; clamped). |
| `/quewi/cue/<num>/seek` | `f` seconds | Jump the playhead to that absolute position in the source file. |

These differ from `/quewi/cue/<num>/set/gainDb` and `/set/pan`, which edit the
cue's *stored* value (persisted, undoable) rather than nudging the live voice.
Use `set/*` to author the show; use `level`/`pan`/`seek` to ride a live mix.

```
# Ride cue 3's level down to -10 dB and pan it slightly left, live:
/quewi/cue/3/level -10.0
/quewi/cue/3/pan   -0.3
```

### Per-cue effects — EQ / compressor / reverb / delay

Set a parameter on a cue's effect, addressed by **effect type** and **parameter
id**. The change is written to the cue (so it persists and the next GO uses it)
**and**, if the cue is currently playing, ridden on the live voice immediately —
so you can dial in a compressor from a remote while the cue plays. The effect is
created with sensible defaults if the cue didn't have one yet.

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/<num>/fx/<type>/<param>` | `f` value (or `T`/`F` for `enabled`) | Set one effect parameter on the cue. |
| `/quewi/cue/<num>/fx/list` | — | Reply on `/quewi/reply/cue/fx` with the cue's chain as JSON (see below). |

`<type>` is `eq`, `compressor`, `reverb`, or `delay`. `<param>` is one of that
effect's parameter ids, or the special `enabled` (`0`/`F` = bypass, `1`/`T` =
on). Unknown type or param are **rejected** (no silent no-op).

| Effect (`<type>`) | Parameter ids (`<param>`) |
|---|---|
| `eq` | `eqN_freq`, `eqN_gain`, `eqN_q`, `eqN_type`, `eqN_enabled` for band N = 1…6 |
| `compressor` | `threshold`, `ratio`, `attack`, `release`, `knee`, `makeup` |
| `reverb` | `roomSize`, `damping`, `width`, `wet` |
| `delay` | `timeL`, `timeR`, `feedback`, `wet` |

```
# Compress cue 3 at 4:1 with a -18 dB threshold, then notch band 3 of its EQ:
/quewi/cue/3/fx/compressor/threshold -18.0
/quewi/cue/3/fx/compressor/ratio       4.0
/quewi/cue/3/fx/eq/eq3_freq          900.0
/quewi/cue/3/fx/eq/eq3_gain           -6.0
/quewi/cue/3/fx/eq/eq3_q               2.0
# Bypass the reverb:
/quewi/cue/3/fx/reverb/enabled         0
```

The `fx/list` reply is JSON so a remote can build a live UI — each effect's
type, name, enabled flag, and every parameter with its current value + range:

```json
{ "cue": 3.0, "effects": [
  { "type": "compressor", "name": "Compressor", "enabled": true,
    "params": [ { "id": "ratio", "label": "Ratio", "value": 4.0, "min": 1.0, "max": 20.0 }, … ] } ] }
```

There is intentionally **no way to remote-drive the audio editor window** — the
editor is a local GUI, but its effect *parameters* are exactly what these verbs
reach, with no need for the editor to be open. (One effect of each type per cue
for now — the rack is track-0 of the cue's editor session.)

### Soundboard (cart)

The cart view is a tap-to-fire **soundboard** — a grid of colour-coded pads, each
bound to a cue, MIDI-pad style. Toggle to it from the View menu, or push it to the
front with `/quewi/cart/show`. Pads are addressed either by a **flat row-major
index** (0 = top-left, counting left-to-right then top-to-bottom) or by explicit
**row + column** (both 0-based).

| Address | Args | Effect |
|---|---|---|
| `/quewi/cart/fire` | `i` index | Fire the pad at that flat index (row-major, 0-based) |
| `/quewi/cart/fire` | `i` row, `i` col | Fire the pad at that row + column (both 0-based) |
| `/quewi/cart/stop` | — | Stop everything the board started (audio fade + lighting blackout + video stop) |
| `/quewi/cart/show` | — | Bring the soundboard to the front |
| `/quewi/cart/layer` | `i` index | Switch the soundboard to that **layer** (0-based). |

Index / row / col / layer accept any numeric type (`i / f / h / d`). A pad with no
cue bound is a no-op. Locally, pads also fire from a keyboard **hotkey** or an
assigned **MIDI note** (set per pad in Edit Layout → click a pad). The OSC paths
above are the network equivalent — handy for a tablet remote, a Stream Deck, or a
lighting console macro.

**Layers:** the soundboard holds multiple switchable pages of pads ("layers" —
e.g. Act 1 / Act 2 / spot FX). Only the active layer fires (locally or over OSC),
so `/quewi/cart/fire` always targets the layer currently shown. Use
`/quewi/cart/layer <i>` to flip pages remotely before firing. `firePadAt` /
`firePadIndex` address pads within the active layer.

```
# Fire the third pad (flat index 2), then the pad at row 1, column 0:
/quewi/cart/fire 2
/quewi/cart/fire 1 0
```

### Cue editing

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/add` | `s` type, optional `f` number, optional `s` name | Append a new cue. Type keys: `audio`, `memo`, `osc`, `fade`, `group`, `wait`, `light`, `light-fade`, `video`, `image`, `text`, `midi`, `msc`, `start`, `stop`, `goto`, `pause`, `load`, `reset`, `devamp` |
| `/quewi/cue/remove` | `f` cue number | Remove the cue with that number (undoable) |
| `/quewi/cue/<num>/move` | `i` new row | Move the cue to a new 0-based row in the active cue list. Undoable. Posts `/quewi/notify/cue/moved` and `/quewi/notify/cueList/reordered` after the move. |
| `/quewi/cue/<num>/set/<field>` | one value (`i/h/f/d/s/T/F`) | Edit a field on the cue with that number. See the full field reference at the bottom. |

### Cue list / active list

| Address | Args | Effect |
|---|---|---|
| `/quewi/cueList/select` | `s` id or name | Switch the active cue list. Accepts either the UUID string or the list's display name. |

### Workspace file ops

| Address | Args | Effect |
|---|---|---|
| `/quewi/workspace/new` | — | Discard current show and start a new empty one (no save prompt — coordinate save state from your controller) |
| `/quewi/workspace/open` | optional `s` path | If a path is given, load that .quewi file. With no args, opens the file picker on the quewi machine. |
| `/quewi/workspace/save` | — | Save to the current path (or open Save-As if untitled) |

### Undo / redo

| Address | Args | Effect |
|---|---|---|
| `/quewi/undo` | — | Step back through the undo stack |
| `/quewi/redo` | — | Step forward through the undo stack |

---

## Queries (peer → quewi, quewi replies)

| Send | Reply address | Reply args |
|---|---|---|
| `/quewi/query/version` | `/quewi/reply/version` | `s` version string |
| `/quewi/query/showName` | `/quewi/reply/showName` | `s` workspace name |
| `/quewi/query/cueLists` | `/quewi/reply/cueLists` | repeating `s s` — list id, list name |
| `/quewi/query/cues` | `/quewi/reply/cues` **or** `/quewi/reply/cues/chunk` (see below) | When the full JSON fits in a single safe-sized UDP datagram (~16 KB or smaller), `/quewi/reply/cues s` ships it intact — the existing shape every remote already speaks. When the payload is bigger, quewi splits it across multiple `/quewi/reply/cues/chunk i i s` messages (chunk index, total chunks, partial JSON). Remotes concatenate chunks in `index` order and parse the assembled string. **Backward-compatible**: small workspaces still see the original single-message reply. |
| `/quewi/query/cue <num>` | `/quewi/reply/cue` | `s` JSON of one cue. No reply if not found. |
| `/quewi/query/playingCues` | `/quewi/reply/playingCues` | `s` JSON array of currently-playing cues: `[{id, type, number, state}, …]`. Use this to rebuild a "now playing" view after a reconnect, or to drive a Fade All button against ground truth (instead of accumulating from notify events that could have been lost). `state` matches `/quewi/notify/cue/playback`. |
| `/quewi/query/workspace` | `/quewi/reply/workspace` | `s` JSON `{name, path, dirty, lastSavedTs}`. `dirty` is `true` when there are unsaved edits. `lastSavedTs` is Unix epoch seconds (0 if untitled). Use this to render "modified" badges and show the absolute file path. |
| `/quewi/query/cueListDetails` | `/quewi/reply/cueListDetails` | `s` JSON `[{id, name, cueCount, isActive}, …]`. Richer alias for `/quewi/query/cueLists` — useful for cue-list picker UIs that need cue counts + which list is currently active without a follow-up round trip. The original `/quewi/query/cueLists` (id/name pairs) stays for backward compat. |

---

## Subscriptions (peer → quewi, quewi pushes back)

### Subscribe / unsubscribe

| Send | Effect |
|---|---|
| `/quewi/subscribe` | optional `s` pattern (default `/quewi/notify/*`). Registers the sender's host:port. Sending twice is a no-op. **Server replies with `/quewi/reply/subscribe <pattern s> <count i>`** so a remote can confirm the packet landed (useful over flaky Wi-Fi). |
| `/quewi/unsubscribe` | optional `s` pattern. Empty pattern removes all of this peer's subscriptions. |

Subscriptions live in memory only. If quewi restarts, re-subscribe. (Run a heartbeat loop and reconnect on timeout.)

### Notifications pushed to subscribers

| Address | Args | When |
|---|---|---|
| `/quewi/notify/workspace/changed` | — | Show loaded, closed, or created. Best response: re-query everything. |
| `/quewi/notify/cueList/active` | `s s` id, name | Active cue list switched |
| `/quewi/notify/cue/added` | `s i d` cue id, row, **cue number** | A cue was inserted. The cue number was added as a third argument in v0.9.57 so remotes can immediately `/quewi/query/cue <num>` without re-querying the whole list. Older remotes that only parsed the first two args still work. |
| `/quewi/notify/showName/changed` | `s` display name (with `*` suffix when dirty) | Pushed whenever the window-title display name changes — file opens, file saves, or dirty-state transitions. Lets remotes mirror the title bar without polling. |
| `/quewi/notify/cue/removed` | `s` cue id | A cue was deleted |
| `/quewi/notify/cue/changed` | `s s` cue id, JSON | Any field of an existing cue changed. JSON matches `/quewi/query/cue`. |
| `/quewi/notify/cue/state` | `s s d` cue id, state, number | Cue transport state. `state` is `"fired"` when GoEngine fires a cue, or `"finished"` when the cue's effect completes — `"finished"` is emitted for audio (when the voice ends), light-fade, fade, wait (after their declared duration), and for instant cues (memo / osc / midi / msc / start / stop / goto / pause / load / reset / devamp / light). Video and group `"finished"` aren't pushed yet — the controller can poll `/quewi/query/cue` if it needs that signal. |
| `/quewi/notify/cue/playback` | `s s d d d` cue id, state, elapsed, remaining, position | **4 Hz heartbeat** while any audio cue is playing. `state` ∈ `"playing"`, `"paused"`, `"fading-out"`. `elapsed` is seconds since fire (after preWait). `remaining` is seconds until natural finish, or `-1.0` if unknown (loops, indefinite). `position` is the playhead in the source file, in seconds. Stops automatically when no audio voices are alive. Designed for transport progress bars on remote controllers — sized so a remote can interpolate between ticks for sub-250-ms updates. |
| `/quewi/notify/cue/moved` | `s i i` cue id, old row, new row | Pushed after a `/quewi/cue/<num>/move` completes. |
| `/quewi/notify/cueList/reordered` | `s s` list id, JSON ordered ids | Pushed alongside `/notify/cue/moved` — lets a remote update its cache of row order without re-fetching every cue. |
| `/quewi/notify/workspace/dirty` | `T` / `F` | Pushed on dirty-state transitions: `T` when the user edits anything, `F` when a save makes it clean. Pairs with the existing `/quewi/notify/workspace/changed` (full reload) for two distinct semantics. |

---

## Cue JSON shape

Replies and `/quewi/notify/cue/changed` pushes encode a cue as a single OSC string containing this JSON:

```json
{
  "id":       "{uuid}",
  "type":     "audio",
  "number":   3.5,
  "name":     "Overture",
  "preWait":  0.0,
  "postWait": 0.0,
  "notes":    "",
  "armed":    true,

  // type-specific keys vary — see the field reference below.
  "filePath":       "S:/OST/01 OVERTURE.wav",
  "gainDb":         0.0,
  "fadeInSeconds":  0.0,
  "trimInSeconds":  0.0,
  "loop":           false
}
```

Always check `"type"` before reading type-specific fields.

---

## Complete field reference

`/quewi/cue/<number>/set/<field>  <value>` works for every field listed
in this section. The OSC argument type column shows what to send; quewi
coerces numeric types (`i/h/f/d`) to whatever the field expects.

### Common base fields (every cue type)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `name` | `s` | — | Human-readable cue name |
| `number` | `f` / `i` | ≥ 0 | Cue number used for display and the `/cue/<n>/...` routing key |
| `preWait` | `f` | seconds, ≥ 0 | Delay before the cue fires |
| `postWait` | `f` | seconds, ≥ 0 | Delay after firing before the continue logic runs |
| `continueMode` | `i` | 0 = Don't continue, 1 = Auto-continue, 2 = Auto-follow | `1` fires the next cue **immediately** on GO (after pre-wait); `2` fires the next cue only **after this cue's action finishes** (audio/video reaches its end, or the duration elapses), then post-wait |
| `notes` | `s` | — | Free-form notes |
| `armed` | `T` / `F` (or `i` 0/1) | — | When false the cue is skipped on GO |
| `color` | `s` | `#AARRGGBB` hex | Row tint colour |

### AudioCue (`type: "audio"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `filePath` | `s` | absolute path | Audio file to play |
| `durationSeconds` | `d` | seconds | **Read-only.** Total source-file duration. Populated by quewi from file metadata when `filePath` decodes; absent before then. Not settable via `/cue/<n>/set/durationSeconds` — remotes use it to scale a transport progress bar against the `/notify/cue/playback` elapsed/remaining values. |
| `gainDb` | `f` | dB | Output gain |
| `fadeInSeconds` | `f` | seconds, ≥ 0 | Fade-in on start |
| `fadeOutSeconds` | `f` | seconds, ≥ 0 | Fade-out on stop |
| `trimInSeconds` | `f` | seconds, ≥ 0 | Start offset into the file |
| `trimOutSeconds` | `f` | seconds, ≥ 0 | End trim from the file end |
| `pan` | `f` | -1.0 (L) … +1.0 (R) | Stereo pan |
| `loop` | `T` / `F` | — | Loop playback |
| `outputDeviceId` | `s` | device id | Audio output device |
| `objectAudio` | `T` / `F` | — | Enable object-based (spatial) audio routing |
| `speakerPatchId` | `s` (UUID) | — | Speaker patch used for object-audio rendering |
| `objAzimuth` | `f` | degrees, -180 … +180 | Source azimuth |
| `objElevation` | `f` | degrees, -90 … +90 | Source elevation |
| `objSpread` | `f` | 0.0 … 1.0 | Source spread (0 = point, 1 = omni) |

### FadeCue (`type: "fade"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `targetId` | `s` (UUID) | — | Cue whose parameter is faded |
| `parameter` | `s` | e.g. `"gainDb"`, `"opacity"` | Field on the target to fade |
| `targetValue` | `f` | parameter-dependent | Value at the end of the fade |
| `durationSeconds` | `f` | seconds, ≥ 0 | Fade duration |

### WaitCue (`type: "wait"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `durationSeconds` | `f` | seconds, ≥ 0 | Time to wait before continuing |

### GroupCue (`type: "group"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `mode` | `i` | 0 = FireAll, 1 = FireFirst, 2 = Timeline | How children are fired |
| `stepInterval` | `f` | seconds, ≥ 0 | Interval between staggered child fires |
| `childIds` | `s` (csv UUIDs) | — | Ordered ids of child cues |
| `childOffsets` | `s` (csv floats) | seconds | Per-child time offsets, parallel to `childIds` |

### MemoCue (`type: "memo"`)

No type-specific fields — inherits the base only.

### Targeting cues (`type: "start" | "stop" | "goto" | "pause" | "load" | "reset" | "devamp"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `targetId` | `s` (UUID) | — | The cue this control cue acts upon |

### OscCue (`type: "osc"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `address` | `s` | OSC path, e.g. `/track/1/mute` | OSC address pattern |
| `host` | `s` | hostname or IP | Destination host |
| `port` | `i` | 0 … 65535 | Destination port |
| `transport` | `i` | 0 = UDP, 1 = TCP | Transport protocol |
| `rawArgs` | `s` | csv tokens — `true`/`false`/`nil`/`inf` → typed tags; ints → `i`/`h`; floats → `f`; quoted strings → `s` | Auto-typed OSC argument list |

### LightCue (`type: "light"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `universe` | `i` | typically 1 … | DMX universe number |
| `channels` | `s` (JSON map) | keys "1" … "512", values 0 … 255 | Channel → DMX value map |

### LightFadeCue (`type: "light-fade"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `targetId` | `s` (UUID) | — | The LightCue to fade toward |
| `durationSeconds` | `f` | seconds, ≥ 0 | Fade duration |

### VideoCue (`type: "video"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `screenIndex` | `i` | 0 … N-1 | Output screen index |
| `posX` | `f` | 0.0 … 1.0 | Surface X (normalized) |
| `posY` | `f` | 0.0 … 1.0 | Surface Y (normalized) |
| `posW` | `f` | 0.0 … 1.0 | Surface width (normalized) |
| `posH` | `f` | 0.0 … 1.0 | Surface height (normalized) |
| `opacity` | `f` | 0.0 … 1.0 | Surface opacity |
| `filePath` | `s` | absolute path | Video file |
| `loop` | `T` / `F` | — | Loop playback |

### ImageCue (`type: "image"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `screenIndex` | `i` | 0 … N-1 | Output screen index |
| `posX` … `posH`, `opacity` | `f` | as VideoCue | Surface placement and opacity |
| `filePath` | `s` | absolute path | Image file |

### TextCue (`type: "text"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `screenIndex` | `i` | 0 … N-1 | Output screen index |
| `posX` … `posH`, `opacity` | `f` | as VideoCue | Surface placement and opacity |
| `text` | `s` | — | Displayed text |
| `fontPixelSize` | `i` | pixels, > 0 | Font pixel size |
| `textColor` | `s` | `#AARRGGBB` hex | Text colour |

### MidiCue (`type: "midi"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `portName` | `s` | MIDI output port name | Destination MIDI port |
| `bytes` | `s` | hex string, separators ignored — `"90 3C 7F"` | Raw MIDI message bytes |

### MscCue (`type: "msc"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `portName` | `s` | MIDI output port name | Destination MIDI port |
| `deviceId` | `i` | 0 … 127 (0x7F = all-call) | MSC device ID |
| `commandFormat` | `i` | e.g. 0x01 Lighting, 0x10 Sound, 0x7F All | MSC command format |
| `command` | `i` | e.g. 0x01 GO, 0x02 STOP, 0x03 RESUME | MSC command |
| `qNumber` | `s` | ASCII | Q_number field |
| `qList` | `s` | ASCII | Q_list field |
| `qPath` | `s` | ASCII | Q_path field |

---

## Recipe: mirror the cue list in real time

The standard pattern for a lighting console, secondary monitor, or any "show me what quewi is doing" app:

```python
from pythonosc.udp_client import SimpleUDPClient
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer
import json

QUEWI_HOST = "192.168.1.50"
QUEWI_PORT = 53535
MY_PORT    = 53536          # we listen here for replies + pushes

client = SimpleUDPClient(QUEWI_HOST, QUEWI_PORT)
cues = {}                   # cue_id → cue dict

def on_cues_reply(addr, json_str):
    for c in json.loads(json_str):
        cues[c["id"]] = c
    redraw()

def on_cue_changed(addr, cue_id, json_str):
    cues[cue_id] = json.loads(json_str)
    redraw()

def on_cue_added(addr, cue_id, row):
    client.send_message("/quewi/query/cue", [float(row)])

def on_cue_removed(addr, cue_id):
    cues.pop(cue_id, None)
    redraw()

def on_cue_state(addr, cue_id, state, number):
    print(f"Cue {number} ({cue_id}): {state}")
    # update your "now playing" indicator here

def on_workspace_changed(addr):
    cues.clear()
    client.send_message("/quewi/query/cues", [])

dispatcher = Dispatcher()
dispatcher.map("/quewi/reply/cues",               on_cues_reply)
dispatcher.map("/quewi/notify/cue/changed",       on_cue_changed)
dispatcher.map("/quewi/notify/cue/added",         on_cue_added)
dispatcher.map("/quewi/notify/cue/removed",       on_cue_removed)
dispatcher.map("/quewi/notify/cue/state",         on_cue_state)
dispatcher.map("/quewi/notify/workspace/changed", on_workspace_changed)

# 1. Subscribe first — that way you don't miss anything between
#    the initial pull and the subscription taking effect.
client.send_message("/quewi/subscribe", [])

# 2. Pull the initial state.
client.send_message("/quewi/query/cues", [])

# 3. Serve forever — replies and pushes both land here.
BlockingOSCUDPServer(("0.0.0.0", MY_PORT), dispatcher).serve_forever()
```

Same pattern works in any language with an OSC client.

---

## Recipe: build a cue from scratch

```python
client.send_message("/quewi/cue/add", ["audio", 4.0, "Bird outside"])
client.send_message("/quewi/cue/4/set/filePath",     "C:/sfx/sparrow.wav")
client.send_message("/quewi/cue/4/set/gainDb",       -3.0)
client.send_message("/quewi/cue/4/set/fadeInSeconds", 1.5)
client.send_message("/quewi/cue/4/set/loop",          True)
client.send_message("/quewi/workspace/save", [])
```

Every step is undoable through `/quewi/undo`.

---

## What's NOT yet exposed over OSC

So your controller doesn't try and fail silently:

- **Patch editing** — speaker patches, DMX universe patches, projection mapping are all GUI-only.
- **Preferences** — listen port/interface, theme, and keyboard shortcuts must be set on the quewi machine (the OSC port/interface are also overridable via the `osc/udpPort` / `osc/listenAddress` QSettings keys — see *Notes on the network*).
- **Script viewer & command palette** — the script window and the command-palette dispatch surface aren't on the wire.
- **Live lighting / video ride** — DMX is controllable per-cue (`light` / `light-fade` cues, `/set/channels`) and via `/quewi/lighting/blackout|fadeOut`, but there's no direct "set universe N channel C to value V" live verb yet; video supports `/set/opacity`, `/set/pos*` (stored) and `/quewi/video/fadeOut|stop`, but no live opacity-ride verb. Most operators automate these as cues instead.
- **Soundboard pad *editing*** — pads fire and layers switch over OSC, but binding a cue to a pad / restyling a pad is GUI-only.
- **Video / group "finished" push** — `/quewi/notify/cue/state finished` covers audio, light-fade, fade, wait, and all instant cue types. Video and group completion needs cue↔voice tracking on VisualCue and child completion bookkeeping on GroupCue respectively; those land in v1.1+.

Everything else — fire, navigate (next/previous/reset), add/remove/**move**/edit-any-field, switch lists, switch soundboard layers, ride a live mix (level/pan/seek), **set EQ/compressor/reverb/delay params (stored + live)**, open/save, undo/redo, and full query/subscribe — **is** on the wire. If you need one of the gaps above, file an issue on the repo.

---

## Notes on the network

Quewi binds UDP on **all IPv4 interfaces** (`0.0.0.0`) so any host on your network can reach it. For real shows:

- Use a wired backstage network that isn't on the public Wi-Fi.
- If you must use Wi-Fi, firewall the port so only your control surface's IP can reach quewi.
- The listen interface and port are configurable in Preferences → OSC; the QSettings keys are `osc/listenAddress` and `osc/udpPort` if you want to override from a script.

TCP/SLIP and WebSocket transports are wired but disabled by default — turn them on in Preferences → OSC → Advanced. Reply messages are sent over UDP only for now, regardless of which transport the request arrived on.
