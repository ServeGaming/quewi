# OSC recipes

Copy-paste examples for common patterns. The
[address reference](reference.md) is the full surface; this page
is the "show me how to do X" companion.

All Python examples use [python-osc](https://pypi.org/project/python-osc/):

```sh
pip install python-osc
```

---

## Fire a cue from a tablet button

```python
from pythonosc.udp_client import SimpleUDPClient

client = SimpleUDPClient("192.168.1.50", 53535)   # quewi's IP

# Press the GO button:
client.send_message("/quewi/go", [])

# Or fire a specific cue by number:
client.send_message("/quewi/cue/start", [12.5])

# Panic if the wheels come off:
client.send_message("/quewi/panic", [])
```

---

## Mirror the cue list in your own UI

The standard "show me what's loaded and what's playing" pattern.

```python
from pythonosc.udp_client import SimpleUDPClient
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer
import json

QUEWI_HOST = "192.168.1.50"
QUEWI_PORT = 53535
MY_PORT    = 53536

client = SimpleUDPClient(QUEWI_HOST, QUEWI_PORT)
cues = {}   # cue_id → cue dict

def on_cues_reply(addr, json_str):
    for c in json.loads(json_str):
        cues[c["id"]] = c
    redraw()

def on_cue_changed(addr, cue_id, json_str):
    cues[cue_id] = json.loads(json_str)
    redraw()

def on_cue_added(addr, cue_id, row):
    # Pull the new cue's full JSON so we don't have to guess
    client.send_message("/quewi/query/cue", [float(row)])

def on_cue_removed(addr, cue_id):
    cues.pop(cue_id, None)
    redraw()

def on_cue_state(addr, cue_id, state, number):
    print(f"Cue {number}: {state}")
    # state is "fired" or "finished"
    update_now_playing(cue_id, state)

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

# Subscribe first so we don't miss anything that happens
# between the initial query and the subscription taking effect.
client.send_message("/quewi/subscribe", [])
client.send_message("/quewi/query/cues", [])

BlockingOSCUDPServer(("0.0.0.0", MY_PORT), dispatcher).serve_forever()
```

---

## Build a cue from scratch

```python
# Create the cue
client.send_message("/quewi/cue/add", ["audio", 4.0, "Bird outside"])

# Edit its fields
client.send_message("/quewi/cue/4/set/filePath", "C:/sfx/sparrow.wav")
client.send_message("/quewi/cue/4/set/gainDb",        -3.0)
client.send_message("/quewi/cue/4/set/fadeInSeconds",  1.5)
client.send_message("/quewi/cue/4/set/loop",           True)

# Save the workspace
client.send_message("/quewi/workspace/save", [])
```

Every step is undoable with `/quewi/undo`.

---

## Tablet "now playing" indicator

Subscribe to cue state, light up an LED on a hardware controller
or change a UI colour when a cue fires / finishes.

```python
def on_cue_state(addr, cue_id, state, number):
    if state == "fired":
        light_led(cue_id, "green")
    elif state == "finished":
        light_led(cue_id, "off")
```

---

## Map a hardware fader to a cue's gain

If your controller can send raw OSC (most can; e.g. TouchOSC,
Lemur, OSC/PILOT):

- Configure the fader to send `/quewi/cue/3/set/gainDb` on
  change, with the fader value (typically 0..127) mapped to
  -60..+12 dB.

Quewi's gain setter live-applies on playing audio voices, so
moving the fader during a show is real-time.

---

## Stream Deck — fire cue 1 to 9

In Companion (Bitfocus):

1. Add a **Generic OSC** module.
2. Set the target IP to quewi's IP, port to `53535`.
3. For each button:
   - Action: **Send OSC integer**
   - Path: `/quewi/cue/start`
   - Argument type: `float`
   - Value: `1.0` (or `2.0`, etc.)

Or in raw Python via the Stream Deck SDK — same OSC calls as the
tablet example.

---

## ETC Eos macro: tell quewi to fire on Eos's cue

In Eos's Macro list, add a network command pointing at quewi's IP:

```
Network OSC TX → IP 192.168.1.50 Port 53535
Address /quewi/cue/start
Args 12.5
```

Bind the macro to whatever event you want — Eos cue X firing,
a softkey press, an "execute" event.

---

## Web controller (browser-based)

Quewi's WebSocket transport (Preferences → OSC → Advanced) lets a
browser app connect directly.

```javascript
import OSC from 'osc-js';

const osc = new OSC({
  plugin: new OSC.WebsocketClientPlugin({
    host: '192.168.1.50',
    port: 53537,
  }),
});
osc.open();
osc.on('open', () => {
  document.querySelector('#go').addEventListener('click', () => {
    osc.send(new OSC.Message('/quewi/go'));
  });

  osc.send(new OSC.Message('/quewi/subscribe'));
});
osc.on('/quewi/notify/cue/state', message => {
  console.log('Cue', message.args[2], 'is', message.args[1]);
});
```

---

## Programmatic show generation

Generate a show from a script — useful for procedurally-built
soundscapes or doing the boring data entry for a long playlist
show.

```python
import json
from pythonosc.udp_client import SimpleUDPClient
client = SimpleUDPClient("127.0.0.1", 53535)

# Start fresh
client.send_message("/quewi/workspace/new", [])

# Build 50 cues from a playlist
with open("playlist.json") as f:
    tracks = json.load(f)

for i, track in enumerate(tracks, start=1):
    n = float(i)
    client.send_message("/quewi/cue/add", ["audio", n, track["title"]])
    client.send_message(f"/quewi/cue/{n}/set/filePath", track["file"])
    client.send_message(f"/quewi/cue/{n}/set/gainDb",   track.get("gain", 0))
    if track.get("crossfade"):
        client.send_message(f"/quewi/cue/{n}/set/fadeOutSeconds", 3.0)
        client.send_message(f"/quewi/cue/{n}/set/continueMode", 1)  # auto

# Save it
client.send_message("/quewi/workspace/save", [])
```

---

## Ride a live mix from a fader bank

Map physical faders / knobs (HeliOSC, TouchOSC, a MIDI-to-OSC bridge)
to live control of whatever's playing. `level` / `pan` / `seek` act on
the cue's *live voice* and no-op safely if it isn't playing, so you can
wire them blind.

```python
# Fader 1 → cue 3 level (0..1 fader mapped to -60..0 dB)
def fader1(addr, value):
    db = -60.0 + value * 60.0
    client.send_message("/quewi/cue/3/level", db)

# Knob 1 → cue 3 pan (-1..+1)
def knob1(addr, value):
    client.send_message("/quewi/cue/3/pan", value * 2.0 - 1.0)

# Jog wheel → scrub cue 3 to an absolute position in seconds
def jog(addr, seconds):
    client.send_message("/quewi/cue/3/seek", seconds)
```

Flip soundboard pages and fire pads the same way:

```python
client.send_message("/quewi/cart/layer", 1)   # switch to layer 2
client.send_message("/quewi/cart/fire", 0)     # fire its top-left pad
```

---

## Heartbeat / reconnect loop

For long-running controllers, the right pattern is a heartbeat
that catches "quewi restarted, my subscription is gone."

```python
import threading, time

last_reply = 0

def on_reply(*args): global last_reply; last_reply = time.time()
dispatcher.map("/quewi/reply/version", on_reply)

def heartbeat():
    while True:
        client.send_message("/quewi/query/version", [])
        time.sleep(5)
        if time.time() - last_reply > 15:
            # Quewi is unreachable or restarted — resubscribe
            client.send_message("/quewi/subscribe", [])
            client.send_message("/quewi/query/cues", [])

threading.Thread(target=heartbeat, daemon=True).start()
```

---

## Companion / Stream Deck pre-canned modules

Generic OSC modules work today. A dedicated Companion module that
hides the OSC details behind named actions (`Go`, `Panic`,
`Set cue gain`, etc.) is on the roadmap. Track or contribute at
[github.com/ServeGaming/quewi](https://github.com/ServeGaming/quewi).
