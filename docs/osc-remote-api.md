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
| `/quewi/lighting/blackout` | — | Blackout all DMX universes |
| `/quewi/video/stop` | — | Stop all video surfaces (audio keeps going) |

### Cue navigation

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/select` | `f` cue number | Move the selection to that cue |
| `/quewi/cue/start` | `f` cue number | Fire that cue directly |
| `/quewi/cue/stop` | `f` cue number | Stop the audio voice for that cue (audio only) |

Cue numbers accept any numeric type (`i / f / h / d`). Use whatever your client emits.

### Cue editing

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/add` | `s` type, optional `f` number, optional `s` name | Append a new cue. Type keys: `audio`, `memo`, `osc`, `fade`, `group`, `wait`, `light`, `light-fade`, `video`, `image`, `text`, `midi`, `msc`, `start`, `stop`, `goto`, `pause`, `load`, `reset`, `devamp` |
| `/quewi/cue/remove` | `f` cue number | Remove the cue with that number (undoable) |
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
| `/quewi/query/cues` | `/quewi/reply/cues` | `s` JSON array of every cue in the active list |
| `/quewi/query/cue <num>` | `/quewi/reply/cue` | `s` JSON of one cue. No reply if not found. |

---

## Subscriptions (peer → quewi, quewi pushes back)

### Subscribe / unsubscribe

| Send | Effect |
|---|---|
| `/quewi/subscribe` | optional `s` pattern (default `/quewi/notify/*`). Registers the sender's host:port. Sending twice is a no-op. |
| `/quewi/unsubscribe` | optional `s` pattern. Empty pattern removes all of this peer's subscriptions. |

Subscriptions live in memory only. If quewi restarts, re-subscribe. (Run a heartbeat loop and reconnect on timeout.)

### Notifications pushed to subscribers

| Address | Args | When |
|---|---|---|
| `/quewi/notify/workspace/changed` | — | Show loaded, closed, or created. Best response: re-query everything. |
| `/quewi/notify/cueList/active` | `s s` id, name | Active cue list switched |
| `/quewi/notify/cue/added` | `s i` cue id, row | A cue was inserted |
| `/quewi/notify/cue/removed` | `s` cue id | A cue was deleted |
| `/quewi/notify/cue/changed` | `s s` cue id, JSON | Any field of an existing cue changed. JSON matches `/quewi/query/cue`. |
| `/quewi/notify/cue/state` | `s s d` cue id, state, number | Cue transport state. `state` is `"fired"` when GoEngine fires a cue, or `"finished"` when an audio cue's voice ends (natural end or `/quewi/cue/stop`). Lighting/video "finished" states aren't pushed yet — those engines don't expose per-cue completion signals. |

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
| `continueMode` | `i` | 0 = DoNotContinue, 1 = AutoContinue, 2 = AutoFollow | How the next cue is triggered |
| `notes` | `s` | — | Free-form notes |
| `armed` | `T` / `F` (or `i` 0/1) | — | When false the cue is skipped on GO |
| `color` | `s` | `#AARRGGBB` hex | Row tint colour |

### AudioCue (`type: "audio"`)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `filePath` | `s` | absolute path | Audio file to play |
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

- **Move / reorder cues** — only add and remove. Moving requires removing and re-adding.
- **Patch editing** — speaker patches, DMX universe patches, projection mapping are all GUI-only.
- **Preferences** — port, theme, shortcuts must be set on the quewi machine itself.
- **Cart grid and script viewer** — not on the wire yet.
- **Command palette** — the UI dispatch surface isn't directly callable.
- **Lighting / video "finished" push** — `/quewi/notify/cue/state` emits `"fired"` for any cue and `"finished"` for audio cues whose voice ends, but lighting and video engines don't have per-cue completion signals yet.

If you need any of these, file an issue on the repo.

---

## Notes on the network

Quewi binds UDP on **all IPv4 interfaces** (`0.0.0.0`) so any host on your network can reach it. For real shows:

- Use a wired backstage network that isn't on the public Wi-Fi.
- If you must use Wi-Fi, firewall the port so only your control surface's IP can reach quewi.
- The listen interface and port are configurable in Preferences → OSC; the QSettings keys are `osc/listenAddress` and `osc/udpPort` if you want to override from a script.

TCP/SLIP and WebSocket transports are wired but disabled by default — turn them on in Preferences → OSC → Advanced. Reply messages are sent over UDP only for now, regardless of which transport the request arrived on.
