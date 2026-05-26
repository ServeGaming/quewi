# Lighting integration

Quewi drives DMX lighting via three transports, configured in
**Preferences → Lighting** and selected per universe in the patch.

---

## Transports

### sACN (E1.31) over Ethernet — default

The standard for modern lighting networks.

- **Multicast** to `239.255.x.y` (universe-derived). Most lighting
  consoles and gateways listen here by default.
- **Unicast** to a specific IP if multicast isn't reachable (some
  Wi-Fi setups block multicast).
- **Source name** advertised on every frame so the receiver can
  identify quewi.

Refresh rate: 44 Hz (standard DMX rate).

### Art-Net over Ethernet

The older protocol, still widely supported. Quewi sends `ArtDmx`
packets to a configured Art-Net node.

- Configure the destination IP per universe in the patch.
- Universes map directly to Art-Net Net + Subnet + Universe.

Use sACN by default; switch to Art-Net only when your hardware
specifically requires it.

### DMX-USB (serial)

For directly-attached USB-to-DMX dongles:

- **Enttec Open DMX USB** — supported. Hand-written serial framing
  matches the Enttec spec.
- **Enttec DMX USB Pro** — supported (same serial protocol family).
- Other USB-DMX devices that present as a USB-serial (RS-485)
  device — likely work; experiment.

One USB device = one universe. For multi-universe shows over USB,
use multiple dongles.

---

## Universe → output mapping

The **patch** binds universes to outputs.

| Universe | Output |
|---|---|
| 1 | sACN multicast |
| 2 | Art-Net to 192.168.1.50 |
| 3 | DMX-USB on COM3 |

Patches live with the workspace, so a show file carries its own
lighting routing.

---

## Light cues

A [Light cue](../cue-types/light.md) is a snapshot of channel
values. Fire one and those values are pushed to the universe.

A [Light Fade cue](../cue-types/light.md) animates from the
current state toward a target snapshot over a duration. The
fade is computed every tick (44 Hz) and the next tick's frame
goes out the wire.

---

## Blackout

`/quewi/lighting/blackout` (OSC) or pressing Panic
(<kbd>Esc</kbd>) drops every active universe to all-zero
immediately. Operator's seatbelt.

---

## Latency

DMX is a continuous-refresh protocol — frames go out 44× per
second whether anything's changed or not. Cue-fire latency is
1 / 44 ≈ 23 ms in the worst case (just missed the last frame),
≈ 0 ms in the best case (just before the next frame). Average:
~11 ms.

That's fine for cue-firing; tight enough that no audience member
will perceive lighting being "behind" a Sound cue.

---

## Limitations

- **No fixture personalities yet.** Quewi works at the raw
  channel level. A 32-channel moving head shows as 32 separate
  channels in the patch — you don't get "address: 1 / personality:
  Mac Aura" semantics. Build channel snapshots manually.
- **No HTP/LTP merging across multiple quewi instances.** Quewi is
  the sole authority for any universe it owns; running two
  quewi instances on the same universe will fight.
- **No effects engine.** Strobes, chases, color macros are
  console-level features. Quewi fires snapshots; chase those at
  the console.

For a stage that needs fixture-level intelligence, drive the
lighting console from quewi via OSC instead — that's what most
shows do anyway.
