# quewi OSC Remote API

A reference card for building any external app (lighting console, iPad
remote, Stream Deck plugin, Companion module, your own custom code)
that talks to quewi over OSC.

---

## TL;DR

| Setting | Value |
|---|---|
| Host | the machine running quewi |
| Port | **53535** (UDP) |
| Protocol | OSC 1.1 |
| Direction | bidirectional — peer sends messages, quewi replies on the same port and pushes live updates back |

Send `/quewi/heartbeat` and you're connected. Send `/quewi/go` and quewi fires its next cue.

```python
# Smallest working example
from pythonosc.udp_client import SimpleUDPClient
client = SimpleUDPClient("127.0.0.1", 53535)
client.send_message("/quewi/go", [])
```

---

## Three things you can do

1. **Trigger** — send commands like `/quewi/go`, `/quewi/panic`, `/quewi/cue/start 3.5`. One-shot, no reply expected.
2. **Query** — ask `/quewi/query/cues` and get a single reply back with the full cue list as JSON.
3. **Subscribe** — send `/quewi/subscribe` once, then receive live push notifications every time the cue list changes (cue fired, name edited, row added/removed). This is how you mirror the cue list in real time.

All three work over the same UDP socket. Quewi reads the source port of every message you send and replies / pushes back to that port — your app just needs to keep one UDP socket open.

---

## Address reference

### Triggers (peer → quewi, no reply)

| Address | Args | Effect |
|---|---|---|
| `/quewi/go` | — | Fire the next cue (same as pressing the GO button) |
| `/quewi/panic` | — | Hard-stop everything: 50 ms audio fade, lighting blackout, all video output closed |
| `/quewi/stop` | — | Alias for `/quewi/panic` |
| `/quewi/pause` | — | Fade audio out over 250 ms |
| `/quewi/heartbeat` | — | No-op, useful as a keepalive ping |
| `/quewi/cue/select` | `f` cue number | Move the selection to that cue |
| `/quewi/cue/start` | `f` cue number | Fire that cue directly |
| `/quewi/cue/stop` | `f` cue number | Stop the engine voice for that cue (audio only) |

Cue numbers accept any numeric type (`i / f / h / d`). Use whatever your client emits.

### Queries (peer → quewi, quewi replies)

| Send | Get back | Reply args |
|---|---|---|
| `/quewi/query/version` | `/quewi/reply/version` | `s` version string |
| `/quewi/query/showName` | `/quewi/reply/showName` | `s` workspace name |
| `/quewi/query/cueLists` | `/quewi/reply/cueLists` | `s s s s …` repeating `id, name` per list |
| `/quewi/query/cues` | `/quewi/reply/cues` | `s` JSON array of every cue in the active list |
| `/quewi/query/cue <num>` | `/quewi/reply/cue` | `s` JSON of one cue. No reply if not found. |

### Subscriptions (peer → quewi, quewi pushes back)

| Send | Effect |
|---|---|
| `/quewi/subscribe` | optional `s` pattern (default `/quewi/notify/*`). Registers the sender's host:port. Sending twice is harmless. |
| `/quewi/unsubscribe` | optional `s` pattern. Empty pattern removes all of this peer's subscriptions. |

Notifications you'll receive:

| Address | Args | When |
|---|---|---|
| `/quewi/notify/workspace/changed` | — | Show loaded, closed, or created. Best response: re-query everything. |
| `/quewi/notify/cueList/active` | `s s` id, name | Active cue list switched |
| `/quewi/notify/cue/added` | `s i` cue id, row | A cue was inserted |
| `/quewi/notify/cue/removed` | `s` cue id | A cue was deleted |
| `/quewi/notify/cue/changed` | `s s` cue id, JSON | Any field of an existing cue changed. JSON is the full updated payload — same shape as `/quewi/query/cue` returns. |

Subscriptions live in quewi's memory only. If quewi restarts, re-subscribe. (Run a heartbeat loop on your side and reconnect on timeout.)

### Field editing (peer → quewi)

```
/quewi/cue/<number>/set/<field>   <value>
```

`<field>` matches `Cue::setField()` keys. Common ones (work on every cue type):

| Field | Type |
|---|---|
| `name` | `s` |
| `number` | `f` / `i` |
| `preWait` | `f` |
| `postWait` | `f` |
| `notes` | `s` |
| `armed` | `T` / `F` (or `i` 0/1) |

Type-specific fields work the same way. For an audio cue:

```
/quewi/cue/3/set/gainDb           -6.0
/quewi/cue/3/set/fadeInSeconds    2.5
/quewi/cue/3/set/loop             T
/quewi/cue/3/set/outputDeviceId   "speakers-foh"
```

Whatever appears in the inspector for that cue type is editable here.

---

## Cue JSON shape

Replies and `/quewi/notify/cue/changed` pushes encode a cue as a single OSC string containing this JSON:

```json
{
  "id": "{uuid}",
  "type": "audio",
  "number": 3.5,
  "name": "Overture",
  "preWait": 0.0,
  "postWait": 0.0,
  "notes": "",
  "armed": true,

  // type-specific keys vary — these come from Cue::toPayload()
  "filePath": "S:/OST/01 OVERTURE.wav",
  "gainDb": 0.0,
  "fadeInSeconds": 0.0,
  "trimInSeconds": 0.0,
  "loop": false
}
```

Always check the `"type"` key before reading type-specific fields.

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

def on_workspace_changed(addr):
    cues.clear()
    client.send_message("/quewi/query/cues", [])

dispatcher = Dispatcher()
dispatcher.map("/quewi/reply/cues",            on_cues_reply)
dispatcher.map("/quewi/notify/cue/changed",    on_cue_changed)
dispatcher.map("/quewi/notify/cue/added",      on_cue_added)
dispatcher.map("/quewi/notify/cue/removed",    on_cue_removed)
dispatcher.map("/quewi/notify/workspace/changed", on_workspace_changed)

# 1. Subscribe for live changes (one shot, takes effect immediately)
client.send_message("/quewi/subscribe", [])

# 2. Pull the initial state
client.send_message("/quewi/query/cues", [])

# 3. Serve forever — replies and pushes both land here
BlockingOSCUDPServer(("0.0.0.0", MY_PORT), dispatcher).serve_forever()
```

Same pattern works in any language that has an OSC client. The key things to remember:

- One UDP socket, both directions.
- Subscribe first, then query — that way you don't miss anything between the initial pull and the subscription taking effect.
- On `workspace/changed`, throw away your cache and re-query.

---

## Notes on the network

quewi binds UDP on **all IPv4 interfaces** (`0.0.0.0`) so any host on your network can reach it. For real shows:

- Use a wired backstage network that's not on the public Wi-Fi.
- If you must use Wi-Fi, firewall the port so only your control surface's IP can reach it.
- The listen interface and port are configurable in Preferences → OSC; the QSettings keys are `osc/listenAddress` and `osc/udpPort` if you want to override from a script.

TCP/SLIP and WebSocket transports are wired but disabled by default. Turn them on in Preferences → OSC → Advanced if your remote needs them. Reply messages are sent over UDP only for now regardless of which transport the request arrived on.
