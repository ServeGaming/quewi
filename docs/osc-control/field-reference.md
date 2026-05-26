# Per-cue field reference

Every settable field on every cue type, with its OSC type tag,
unit / range, and a one-line meaning. Use these names as the
`<field>` segment in `/quewi/cue/<num>/set/<field>`.

The same table also documents what comes back in the cue JSON
payload (`/quewi/query/cue`, `/quewi/notify/cue/changed`) — the
JSON keys are these field names verbatim.

For the live, authoritative version (kept in sync with the C++
source via the `Cue::setField` audit), see the
[address reference page](reference.md#complete-field-reference).

---

## Common fields (every cue type)

| Field | Type | Units / range | Meaning |
|---|---|---|---|
| `name` | `s` | — | Human-readable cue name |
| `number` | `f` / `i` | ≥ 0 | Cue number used for display and the `/cue/<n>/...` routing key |
| `preWait` | `f` | seconds, ≥ 0 | Delay before the cue fires |
| `postWait` | `f` | seconds, ≥ 0 | Delay after firing before the continue logic runs |
| `continueMode` | `i` | 0 = DoNotContinue, 1 = AutoContinue, 2 = AutoFollow | How the next cue is triggered |
| `notes` | `s` | — | Free-form notes |
| `armed` | `T` / `F` | — | When false the cue is skipped on GO |
| `color` | `s` | `#AARRGGBB` hex | Row tint colour |

---

## Per cue-type tables

Open the address reference for the full per-cue-type tables:

- [Audio cue (`"audio"`)](reference.md#audiocue-type-audio)
- [Fade cue (`"fade"`)](reference.md#fadecue-type-fade)
- [Wait cue (`"wait"`)](reference.md#waitcue-type-wait)
- [Group cue (`"group"`)](reference.md#groupcue-type-group)
- [OSC cue (`"osc"`)](reference.md#osccue-type-osc)
- [Light cue (`"light"`)](reference.md#lightcue-type-light)
- [Light fade cue (`"light-fade"`)](reference.md#lightfadecue-type-light-fade)
- [Video cue (`"video"`)](reference.md#videocue-type-video)
- [Image cue (`"image"`)](reference.md#imagecue-type-image)
- [Text cue (`"text"`)](reference.md#textcue-type-text)
- [MIDI cue (`"midi"`)](reference.md#midicue-type-midi)
- [MSC cue (`"msc"`)](reference.md#msccue-type-msc)
- [Targeting cues (`"start"`, `"stop"`, `"goto"`, etc.)](reference.md#targeting-cues-type-start-stop-goto-pause-load-reset-devamp)
- [Memo cue (`"memo"`)](reference.md#memocue-type-memo)

---

## Type tag legend

OSC's type tags determine how a value is encoded on the wire:

| Tag | C / typed name | Use for |
|---|---|---|
| `i` | int32 | Integers up to ±2^31. Cue numbers if they're whole; channel indexes; enum values. |
| `h` | int64 | Bigger integers. Rarely needed. |
| `f` | float32 | Standard float. Use for gain, time, opacity, percent. |
| `d` | float64 | Higher-precision float. Use for cue numbers with decimals (3.50). |
| `s` | string | UTF-8 string. Names, file paths, OSC addresses. |
| `T` | true | Boolean true. No payload. |
| `F` | false | Boolean false. No payload. |
| `b` | blob | Raw bytes. Used for the `bytes` field on MIDI cues. |

Quewi's `setField` is forgiving about numeric types — sending
`i` to a float field auto-converts. The strict types come into
play when sending OSC *out* via an OSC cue, where the receiver
might require a specific tag.

---

## Live-applied vs fire-time

Some fields apply to a currently-playing voice immediately
(without re-firing the cue):

- `gainDb`
- `pan`
- `outputGainsDb` (per-channel output matrix)

Other fields only take effect on the next fire:

- Anything that's a file path, decoder setting, fade-in time,
  loop flag — these are read at fire time and don't update mid-
  playback.

This matches the [Inspector's behaviour](../using-quewi/inspector.md#live-applied-vs-on-fire).
A controller dragging a fader during a show updates gain in
real time; a controller swapping the `filePath` field has to
stop and re-fire to hear it.
