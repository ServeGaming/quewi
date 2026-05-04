# OSC Feature Coverage

Source of truth for what quewi's OSC layer supports. Updated as features land.

Legend: ✅ implemented · 🚧 in progress · ⬜ planned

## OSC 1.0 baseline

| Feature | Status | Notes |
|---|---|---|
| Type tag `i` (int32) | ⬜ | Phase 2 |
| Type tag `f` (float32) | ⬜ | Phase 2 |
| Type tag `s` (string) | ⬜ | Phase 2 |
| Type tag `b` (blob) | ⬜ | Phase 2 |
| Bundles (`#bundle`) | ⬜ | Phase 2 |
| Bundle nesting | ⬜ | Phase 2 |
| Time tags (NTP 64-bit) | ⬜ | Phase 2 |
| Address pattern `?` | ⬜ | Phase 2 |
| Address pattern `*` | ⬜ | Phase 2 |
| Address pattern `[chars]` | ⬜ | Phase 2 |
| Address pattern `{alt,alt}` | ⬜ | Phase 2 |
| UDP transport | ⬜ | Phase 2 |

## OSC 1.1 / extended types

| Feature | Status | Notes |
|---|---|---|
| `h` (int64) | ⬜ | |
| `t` (time tag arg) | ⬜ | |
| `d` (double) | ⬜ | |
| `S` (symbol) | ⬜ | |
| `c` (char) | ⬜ | |
| `r` (RGBA color) | ⬜ | |
| `m` (MIDI) | ⬜ | |
| `T` `F` (true/false) | ⬜ | |
| `N` (nil) | ⬜ | |
| `I` (infinitum) | ⬜ | |
| `[` `]` (arrays) | ⬜ | |
| Pattern `//` (descendant) | ⬜ | |
| TCP transport (SLIP framed, RFC 1055) | ⬜ | Phase 2 |

## Common-practice extensions

| Feature | Status | Notes |
|---|---|---|
| WebSocket transport | ⬜ | Phase 2 |
| Unix domain socket transport | ⬜ | |
| Multicast / broadcast | ⬜ | |
| OSC Query (HTTP/WS namespace) | ⬜ | Phase 2 |
| Live monitor (hex + decoded) | ⬜ | Phase 2 |
| Learn mode (capture → bind) | ⬜ | Phase 2 |
| Dictionary import/export | ⬜ | |
