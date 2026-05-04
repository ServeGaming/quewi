# OSC Feature Coverage

Source of truth for what quewi's OSC layer supports. Updated as features land.

Legend: ✅ implemented · 🚧 in progress · ⬜ planned

## OSC 1.0 baseline

| Feature | Status | Notes |
|---|---|---|
| Type tag `i` (int32) | ✅ | Phase 2 |
| Type tag `f` (float32) | ✅ | Phase 2 |
| Type tag `s` (string) | ✅ | Phase 2 |
| Type tag `b` (blob) | ✅ | Phase 2 |
| Bundles (`#bundle`) | ✅ | Phase 2 |
| Bundle nesting | ✅ | Phase 2 |
| Time tags (NTP 64-bit) | ✅ | Phase 2 (encoded; scheduled execution comes with GoEngine in Phase 6) |
| Address pattern `?` | ✅ | Phase 2 |
| Address pattern `*` | ✅ | Phase 2 |
| Address pattern `[chars]` | ✅ | Phase 2 |
| Address pattern `{alt,alt}` | ✅ | Phase 2 |
| UDP transport | ✅ | Phase 2 (outbound; inbound + dispatcher in next commit) |

## OSC 1.1 / extended types

| Feature | Status | Notes |
|---|---|---|
| `h` (int64) | ✅ | |
| `t` (time tag arg) | ✅ | |
| `d` (double) | ✅ | |
| `S` (symbol) | ✅ | |
| `c` (char) | ✅ | |
| `r` (RGBA color) | ✅ | |
| `m` (MIDI) | ✅ | |
| `T` `F` (true/false) | ✅ | |
| `N` (nil) | ✅ | |
| `I` (infinitum) | ✅ | |
| `[` `]` (arrays) | ✅ | nested arrays supported |
| Pattern `//` (descendant) | ✅ | Phase 2 |
| TCP transport (SLIP framed, RFC 1055) | ⬜ | next commit |

## Common-practice extensions

| Feature | Status | Notes |
|---|---|---|
| WebSocket transport | ⬜ | next commit |
| Unix domain socket transport | ⬜ | |
| Multicast / broadcast | ⬜ | |
| OSC Query (HTTP/WS namespace) | ⬜ | |
| Live monitor (hex + decoded) | ⬜ | |
| Learn mode (capture → bind) | ⬜ | |
| Dictionary import/export | ⬜ | |

## Known gaps

- **Inbound dispatch.** Pattern matching is implemented but no listener routes incoming packets to subscribers yet; landing in the OSC monitor commit.
- **Args UI.** OscCue's `rawArgs` is a single comma-separated text field that auto-types each token. The richer typed-arg editor with explicit type pickers (sketched in design.md §8) is deferred until the OSC monitor lands so we can share widget code with the monitor's decoded view.
- **Shared destinations / patch.** Each OscCue currently carries its own host/port. A shared destinations patch (so one `Eos console` definition is reused across cues) lands when patches generally are introduced (Phase 4 lighting needs them too).
