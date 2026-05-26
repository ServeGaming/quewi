# MIDI & MIDI Show Control

Quewi sends and receives MIDI through RtMidi — cross-platform
binding to CoreMIDI / Windows MIDI / ALSA. MIDI ports show up
automatically when the OS sees the hardware.

---

## MIDI cue (raw bytes)

The [MIDI cue](../cue-types/midi.md) sends arbitrary MIDI bytes
to a named output port. Use for:

- Triggering samples on a hardware sampler / synth
- Sending program change to a guitar processor
- Toggling a recall on a digital mixer that speaks MIDI CC
- Anything you'd otherwise do with a MIDI keyboard

Fields:

| Field | Meaning |
|---|---|
| **Port name** | The MIDI output port. Quewi populates a dropdown from the OS. |
| **Bytes** | Hex string of the MIDI message. Separators (space, comma, newline) are ignored. `90 3C 7F` = Note On, channel 1, middle C, max velocity. |

For multi-byte SysEx messages, just keep going past the
status byte — quewi sends whatever you put in.

---

## MSC cue (MIDI Show Control)

The [MSC cue](../cue-types/midi.md) sends MIDI Show Control —
the standard protocol for inter-device cue triggering used by
high-end lighting consoles, automation systems, and show
controllers.

Fields:

| Field | Meaning |
|---|---|
| **Port name** | MIDI output port |
| **Device ID** | 0..127. `0x7F` (127) = all-call (every device on the wire) |
| **Command format** | What kind of system you're addressing. `0x01` = Lighting, `0x10` = Sound, `0x40` = Process Control, `0x7F` = All |
| **Command** | The MSC verb. `0x01` = GO, `0x02` = STOP, `0x03` = RESUME, `0x04` = TIMED_GO, `0x0B` = STANDBY+, … |
| **Q_number** | ASCII cue number on the target system |
| **Q_list** | ASCII cue list name (optional) |
| **Q_path** | ASCII cue path (optional) |

Use to talk to:

- **ETC Eos** family — set Device ID 0, Command Format Lighting, GO/STOP commands
- **Hog 4** — same idea, target the Hog's MSC device ID
- **GrandMA 2/3** — supports MSC, target the desk's ID
- Any other show controller that speaks MSC

---

## MIDI input — triggers

Quewi can fire cues in response to incoming MIDI. Configure in
**Preferences → MIDI → Triggers**.

Each trigger binds a MIDI message (Note On, Program Change, CC
matching a value, etc.) to a quewi action:

- GO
- Panic / Pause / Fade All
- Select cue N
- Fire cue N
- Set a field on a specific cue (e.g. CC 7 → gainDb on cue 3)

Use this for hardware that doesn't speak OSC: a USB MIDI
footswitch, a MIDI controller's pads, a clip launcher.

---

## Latency

MIDI is single-byte at 31250 baud — about 0.3 ms per byte on the
wire. Add USB-MIDI driver round-trip and the total fire-to-MIDI-
emitted is typically 1-5 ms. Faster than audio cue startup.

---

## Bidirectional

A MIDI port shows up on both the input AND output side. Quewi
can listen for triggers on the same port it sends MSC on — the
hardware bus handles bidirectional traffic natively.

---

## Limitations

- **No MIDI Time Code (MTC) generate or chase yet.** Post-1.0.
  Until then, sync your DAW / video host with a different
  master.
- **No MIDI Clock generate / chase.** Same — post-1.0.
- **No JACK / virtual MIDI port creation on macOS.** Use IAC or
  an external loopback driver if you need quewi to feed another
  app via MIDI on the same machine.
