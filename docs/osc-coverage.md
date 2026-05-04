# OSC Feature Coverage

Source of truth for what quewi's OSC layer supports. Updated as features land.

Legend: ✅ implemented · 🚧 in progress · ⬜ planned

## OSC 1.0 baseline

| Feature | Status |
|---|---|
| Type tag `i` (int32) | ✅ |
| Type tag `f` (float32) | ✅ |
| Type tag `s` (string) | ✅ |
| Type tag `b` (blob) | ✅ |
| Bundles (`#bundle`) | ✅ |
| Bundle nesting | ✅ |
| Time tags (NTP 64-bit) | ✅ encoded — scheduled execution lands with the GoEngine in Phase 6 |
| Address pattern `?` | ✅ |
| Address pattern `*` | ✅ |
| Address pattern `[chars]` | ✅ |
| Address pattern `{alt,alt}` | ✅ |
| UDP transport (out + in) | ✅ |

## OSC 1.1 / extended types

| Feature | Status |
|---|---|
| `h` (int64) | ✅ |
| `t` (time tag arg) | ✅ |
| `d` (double) | ✅ |
| `S` (symbol) | ✅ |
| `c` (char) | ✅ |
| `r` (RGBA color) | ✅ |
| `m` (MIDI) | ✅ |
| `T` `F` (true/false) | ✅ |
| `N` (nil) | ✅ |
| `I` (infinitum) | ✅ |
| `[` `]` (arrays, nested) | ✅ |
| Pattern `//` (descendant) | ✅ |
| TCP transport with SLIP framing (RFC 1055) | ✅ |

## Common-practice extensions

| Feature | Status |
|---|---|
| WebSocket transport (binary + text frames) | ✅ |
| Pattern-matching dispatcher with subscriptions | ✅ |
| Live OSC monitor (hex + decoded view, in/out, transport, peer) | ✅ |
| Multicast / broadcast destinations | ⬜ |
| Unix domain socket transport | ⬜ |
| OSC Query (HTTP/WS namespace introspection) | ⬜ |
| Learn mode (capture → bind to cue) | ⬜ |
| Dictionary import/export | ⬜ |

## Architecture

The codec (`OscCodec`) is pure functions. The engine (`OscEngine`) owns
transport sockets and subscriptions. SLIP framing is stream-state aware
so partial frames straddling TCP chunk boundaries reassemble correctly.
Every send and every receive emits a `packetSeen` signal that the
monitor consumes — observation is decoupled from dispatch so the monitor
never blocks the hot path.

## Known gaps

- **Time-tag scheduling.** Bundles with future time tags currently fire
  immediately on receipt. Honouring them requires the GoEngine's
  scheduler (Phase 6).
- **Args UI.** OscCue's `rawArgs` is still a comma-separated text field.
  Now that the monitor renders arg lists, the typed-arg editor (with
  per-arg type pickers) can share the rendering widget — slated for a
  follow-up commit.
- **Shared destinations / patch.** Each OscCue carries its own
  host/port. A shared destinations patch lands when patches generally
  arrive (Phase 4 lighting needs them too).
- **Inbound listeners are user-toggled in the monitor.** A more
  permanent setup lives in Preferences once that grows real categories.
