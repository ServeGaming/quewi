# Lighting in quewi

## What ships today

quewi outputs DMX over **sACN** (E1.31) multicast. Universe `u`
auto-routes to multicast group `239.255.<u_high>.<u_low>` on UDP port
**5568**, exactly per the spec. Source CID is generated once per app
launch; the source name and priority will be configurable in
Preferences when that pane lands.

The engine ticks at ~44 Hz (matching the DMX refresh rate) and
broadcasts every active universe even between cues, so receivers
holding state stay locked.

Art-Net and DMX-USB outputs are scheduled for follow-up commits — the
engine architecture already accommodates them.

## Cue types

### Light cue (`light`)

Stores a sparse map of `(channel → 0..255)` for one universe.
Channels not in the map are left untouched (delta semantics) — fire a
cue with `channel 1: 255` and only channel 1 changes; everything else
keeps its current live value. Use a Light Fade cue (or a black-out
cue with channels 1..512 all at 0) to clear state.

Inspector:

- Universe spinner
- Editable (channel, value) table — both columns inline-editable
- "+ Channel" inserts a new row; "Remove" deletes selected rows
- Each table edit becomes a single undoable step

### Light Fade cue (`light-fade`)

Targets a Light cue and ramps every channel in that cue's map from
its current live value to the target value over the fade duration.
Channels NOT in the target's map are left alone — same delta semantics
as the Light cue itself.

Inspector:

- Target combo (filtered to Light cues in the active list)
- Duration spinner

## Receivers / verifying output

Any of these will see quewi's traffic on universe 1 if you fire a
Light cue with anything in it:

- **ETC sACNView** (free, Windows/Mac) — single-window receiver
- **QLC+** — open-source lighting console; can show incoming sACN
- **Resolume Arena** — accepts sACN as input
- **ETC EOS** consoles — accept sACN out of the box

Run any receiver on the same network segment as quewi (multicast
doesn't cross routers without IGMP snooping configured). On a typical
home/office wired LAN it just works.

## Roadmap

| Feature | When |
|---|---|
| Art-Net output | Phase 4 follow-up |
| DMX-USB (Enttec Open DMX) over native serial | Phase 4 follow-up |
| Universe patch model (named universes → adapters) | Phase 4 follow-up alongside the OSC patch model |
| Eased fade curves (S-curve, exponential) | Phase 7 polish |
| HTP/LTP merge between cues | Phase 7 polish |
| 32×16 grid channel editor (vs the table) | Phase 7 polish |
