# MIDI & MSC cues

Two related cue types: **MIDI** sends arbitrary MIDI bytes;
**MSC** sends MIDI Show Control messages.

For the system-level MIDI documentation (transports, ports,
input triggers), see [Integration → MIDI](../integration/midi.md).

---

## MIDI cue

Sends raw MIDI bytes to a named output port.

### Inspector fields

| Field | Meaning |
|---|---|
| **Port name** | The MIDI output port (dropdown of OS-discovered ports) |
| **Bytes** | Hex string of the MIDI message. Whitespace, commas, and newlines are ignored. |

### Examples

| Bytes | What it sends |
|---|---|
| `90 3C 7F` | Note On — channel 1, middle C (60), velocity 127 |
| `80 3C 00` | Note Off — channel 1, middle C |
| `B0 07 64` | CC — channel 1, controller 7 (volume), value 100 |
| `C0 05` | Program Change — channel 1, program 5 |
| `F0 00 00 66 14 12 ... F7` | SysEx — Mackie HUI / Logic Control "track 1 select" |

For long SysEx messages, paste the whole thing into Bytes; quewi
sends every byte verbatim.

---

## MSC cue

Sends MIDI Show Control — the standardised protocol most major
lighting consoles and show controllers speak.

### Inspector fields

| Field | Type | Meaning |
|---|---|---|
| **Port name** | string | MIDI output port |
| **Device ID** | 0..127 | Target's MSC device ID (0x7F = all-call) |
| **Command format** | int | What kind of system — 0x01 Lighting, 0x10 Sound, 0x40 Process Control, 0x7F All |
| **Command** | int | MSC verb — 0x01 GO, 0x02 STOP, 0x03 RESUME, 0x04 TIMED_GO, 0x0B STANDBY+, 0x0C STANDBY-, … |
| **Q_number** | string | Target cue number (ASCII) |
| **Q_list** | string | Target cue list (ASCII, optional) |
| **Q_path** | string | Target cue path (ASCII, optional) |

### Talking to common consoles

**ETC Eos / Ion / Element**

- Device ID: `0` (or whatever you set on the Eos)
- Command format: `0x01` (Lighting)
- Command: `0x01` (GO)
- Q_number: the Eos cue number, e.g. `100`

**Hog 4 / Wholehog**

- Device ID: as configured on the Hog
- Command format: `0x01` (Lighting)
- Command: `0x01` (GO)
- Q_number: Hog cue number

**GrandMA 2/3**

- Device ID: as configured
- Command format: `0x01` (Lighting)
- Command: `0x01` (GO)
- Q_number: MA cue number

Some consoles want `commandFormat = 0x7F` (All) instead of the
specific subsystem — check the console's MSC documentation.

### Command quick reference

| Hex | Name |
|---|---|
| `01` | GO |
| `02` | STOP |
| `03` | RESUME |
| `04` | TIMED_GO |
| `05` | LOAD |
| `06` | SET |
| `07` | FIRE (macro) |
| `08` | ALL_OFF |
| `09` | RESTORE |
| `0A` | RESET |
| `0B` | GO_OFF |
| `0C` | GO_JAM_CLOCK |
| `10..1F` | Sound system commands |

---

## Building a MIDI message live

Use the [MIDI Monitor](../using-quewi/inspector.md) (Tools menu)
to sniff incoming MIDI from a hardware controller, capture the
bytes, paste into the cue. Much faster than reading the MIDI spec.
