# quewi OSC Remote API

This is the spec for **driving quewi from a remote** — your iPad app,
a Stream Deck plugin, a Bitfocus Companion module, or any other OSC
client. quewi listens on **UDP port 53535** by default the moment the
app launches; that port is configurable in Preferences (when the
Preferences → OSC pane lands; until then the port is hard-wired and
matches this document).

This is distinct from the OSC layer's full coverage matrix in
[`osc-coverage.md`](osc-coverage.md), which describes the codec and
transport features that lower-level OSC cues use to talk to *other*
gear. This doc is just the public control surface aimed at remotes.

---

## Connection

| Field | Value |
|---|---|
| Transport | UDP |
| Default port | **53535** |
| Address pattern | OSC 1.1 (with `?`, `*`, `[]`, `{}`, `//`) |
| Encoding | All standard OSC 1.0 + 1.1 type tags |
| Bundles | Accepted; elements dispatched in order (time-tag scheduling lands with the GoEngine in Phase 6) |

If you're testing from a Mac, `oscchief` works:

```bash
oscchief send 127.0.0.1 53535 /quewi/go
```

From Python:

```python
from pythonosc.udp_client import SimpleUDPClient
c = SimpleUDPClient("192.168.1.50", 53535)
c.send_message("/quewi/go", [])
c.send_message("/quewi/cue/start", [3.5])
```

---

## Addresses

### Transport

| Address | Args | Effect |
|---|---|---|
| `/quewi/go` | none | Fires the next cue. Equivalent to pressing the GO button (Spacebar). |
| `/quewi/panic` | none | Hard-stop everything: 50 ms audio fade, lighting blackout, all video output closed. |
| `/quewi/stop` | none | Alias for `/quewi/panic`. |
| `/quewi/pause` | none | Soft pause — fades audio out over 250 ms. (True sample-accurate pause arrives with the GoEngine in Phase 6.) |
| `/quewi/heartbeat` | none | No-op. Useful as a keepalive ping in remotes. |

### Cue control by number

| Address | Args | Effect |
|---|---|---|
| `/quewi/cue/select` | float (cue number) | Move the selection to the cue with that number. |
| `/quewi/cue/start` | float (cue number) | Fire that cue immediately, regardless of selection. |
| `/quewi/cue/stop` | float (cue number) | Stop the engine voice currently bound to that cue (audio only for now). |

Cue numbers are floating-point in quewi (1, 2, 2.5, 3, 10) — you can
send any of `i / f / h / d` and quewi will accept it. Whole numbers
sent as integers work fine.

```
oscchief send 127.0.0.1 53535 /quewi/cue/start 3.5
```

### Errors / responses

(See **Query / reply** and **Subscribe / push notifications** below — replies and live state updates are wired as of v0.9.20.)

---

## Query / reply

quewi replies to query messages back to the **source UDP port of the request** on a `/quewi/reply/...` address. Your client only needs to listen on the port it sent from; no reply-to argument required.

| Request | Reply | Args |
|---|---|---|
| `/quewi/query/version` | `/quewi/reply/version` | `s` — version string |
| `/quewi/query/showName` | `/quewi/reply/showName` | `s` — workspace name |
| `/quewi/query/cueLists` | `/quewi/reply/cueLists` | repeating `s s` — id, name per list |
| `/quewi/query/cues` | `/quewi/reply/cues` | `s` — JSON array of every cue in the active list |
| `/quewi/query/cue <num>` | `/quewi/reply/cue` | `s` — JSON of one cue, or no reply if not found |

The cue JSON shape:

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
  /* …plus type-specific fields from toPayload() */
}
```

## Subscribe / push notifications

Subscribe once and the server pushes state changes to your UDP source port for the duration of the session.

| Address | Args | Effect |
|---|---|---|
| `/quewi/subscribe` | optional `s` (pattern; default `/quewi/notify/*`) | Register the sender's `host:port` for notifications. Dedup'd; sending twice is a no-op. |
| `/quewi/unsubscribe` | optional `s` (pattern) | Drop the subscription. Empty pattern removes ALL of this peer's subscriptions. |

Pushed notifications:

| Address | Args | When |
|---|---|---|
| `/quewi/notify/workspace/changed` | none | A show was loaded, closed, or created. Re-query everything. |
| `/quewi/notify/cueList/active` | `s s` — list id, name | The active cue list switched. |
| `/quewi/notify/cue/added` | `s i` — cue id, row index | A cue was inserted. |
| `/quewi/notify/cue/removed` | `s` — cue id | A cue was deleted. |
| `/quewi/notify/cue/changed` | `s s` — cue id, JSON | Any field of an existing cue changed. JSON is the full updated payload. |

A typical lighting-app boot sequence:

```python
client.send_message("/quewi/subscribe", [])
client.send_message("/quewi/query/cueLists", [])
client.send_message("/quewi/query/cues", [])
# then handle /quewi/notify/cue/changed in real time
```

## Field setters

Edit any cue field live by number:

```
/quewi/cue/<number>/set/<field>  <value>
```

`<field>` is the same key used by `Cue::setField()` — common ones:

| Field | Type |
|---|---|
| `name` | string |
| `number` | float / int |
| `preWait` | float / int |
| `postWait` | float / int |
| `notes` | string |
| `armed` | bool (`T`/`F` or `i` 0/1) |

Type-specific fields (e.g. `gainDb`, `fadeInSeconds`, `outputDeviceId` on audio cues) work the same way — anything `setField()` accepts.

---

## Building a remote — design notes

Three patterns we recommend:

1. **GO button + cue list mirror.** The simplest useful remote: one big
   GO button that sends `/quewi/go`, plus a panic. Add a heartbeat at
   ~1 Hz so the remote can show "connected" / "lost" status.
2. **Dedicated cue trigger pads.** Each pad sends
   `/quewi/cue/start <number>`. Useful for sound-effect surfaces and
   show-bibles where the operator wants direct access to specific cues.
3. **Selection navigator.** Up/down arrows that send
   `/quewi/cue/select` with the previous/next cue's number; pressing
   GO from the remote then fires whatever's selected. This requires
   the remote to know the cue list — outbound state pushes will
   eventually carry that.

If you're integrating an existing **ETC + QLab-style remote** like the
one this project's author built for iOS, the address scheme above
maps 1:1 against QLab's `/cue/<num>/start`, `/cue/selected/start`,
`/go`, etc. — the prefix is `/quewi` instead of `/cue` to keep the
namespaces distinct, but the semantics are identical so most of your
control surface should port directly.

---

## Security / network notes

quewi binds the UDP listener on **all** IPv4 interfaces (`0.0.0.0`).
That means anyone on your network can fire cues if they find the port.
For real shows:

- Run quewi on a wired backstage network that's *not* on the public
  Wi-Fi.
- If you must expose it on a wireless network, put quewi behind a
  firewall that only allows OSC traffic from your control surface's IP.
- A Preferences toggle to bind the listener to a specific interface
  is on the Phase 7 polish list.

---

## What's planned

These are not yet implemented; documented here so remotes can be
written forward-compatibly:

| Address | Purpose |
|---|---|
| `/quewi/cue/<num>/state` | Outbound — state change events. |
| `/quewi/active/list` | Outbound — list of currently running cues. |
| `/quewi/list/select <name>` | Switch the active cue list (when multi-list lands in Phase 7). |
| `/quewi/showmode/lock <bool>` | Toggle Show Mode lock (Phase 7). |

If you're building against any of these, ping the project — happy to
prioritise based on real remote use cases.
