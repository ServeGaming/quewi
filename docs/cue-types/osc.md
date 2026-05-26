# OSC cue

Sends an OSC message to a destination when fired. This is for
quewi → external system communication (controlling Eos, sending
to Resolume, triggering on a hardware controller, etc.).

For **external system → quewi** control, see
[OSC remote control](../osc-control/reference.md) — that's the
opposite direction.

## Inspector fields

| Field | Meaning |
|---|---|
| **Host** | Destination IP or hostname |
| **Port** | Destination port |
| **Transport** | UDP or TCP/SLIP |
| **Address** | OSC path, e.g. `/cue/1/start`, `/eos/cue/100/fire` |
| **Args** | Comma-separated argument list, auto-typed |

## Argument auto-typing

Each token in the **Args** field is converted to an OSC argument
of the most natural type:

| Token | Type | OSC tag |
|---|---|---|
| `42` | int32 | `i` |
| `42.0` or `3.14` | float32 | `f` |
| `"hello"` (quoted) | string | `s` |
| `hello` (bare) | string | `s` |
| `true` | bool true | `T` |
| `false` | bool false | `F` |
| `nil` | nil | `N` |
| `inf` | infinitum | `I` |

For more precise control, use the raw `rawArgs` field in the
Inspector — it accepts the same syntax with explicit type tag
hints (`f:1.5`, `i:42`, `s:"hello"`).

## Examples

**Trigger an ETC Eos cue:**

| Field | Value |
|---|---|
| Host | `192.168.1.100` |
| Port | `8000` |
| Address | `/eos/cue/100/fire` |
| Args | *(empty)* |

**Set a fader on a digital mixer:**

| Field | Value |
|---|---|
| Host | `mixer.local` |
| Port | `10024` |
| Address | `/ch/01/mix/fader` |
| Args | `0.75` |

**Multi-argument color command:**

| Field | Value |
|---|---|
| Address | `/light/4/color` |
| Args | `255, 128, 0` |

## OSC bundles

Currently the OSC cue sends a single OSC message per fire. For
bundles (multiple messages with one network packet, optionally
with a future time tag), use a Group cue containing multiple
OSC cues — quewi groups them on the wire automatically when
they all fire on the same event-loop turn.
