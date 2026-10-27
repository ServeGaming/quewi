# Plan format (build_show.py)

A plan is one JSON file. Minimal:

```json
{
  "show": {"name": "Spring Revue", "output": "D:/Shows/Spring Revue/Spring Revue.quewi"},
  "cues": [
    {"file": "D:/Music/01 Overture.wav",
     "triggers": [{"at": 0.4, "name": "Overture", "do": "go"}]}
  ]
}
```

`show.output` may be relative to the plan file. `--out` on the command line
wins over it. `.quewi` is added if missing.

## Lists

`"cues": [...]` makes one list called `Main` (or `show.listName`). For
several lists:

```json
"lists": [ {"name": "Act 1", "cues": [...]}, {"name": "Act 2", "cues": [...]} ]
```

## Cues

Every cue: `number` (default: previous + 1, from 1), `name`, `notes`,
`color` (`"#d97706"`), `preWait`, `postWait` (seconds), `armed` (default
true), `continue`: `"none"` (default) / `"auto-continue"` (fire the next cue
`postWait` after this one starts) / `"auto-follow"` (fire the next cue when
this one finishes).

**Audio cue** (`"type": "audio"`, implied by `file`):

| key | meaning |
|---|---|
| `file` | the track (absolute, or relative to the plan) — any format quewi plays |
| `name` | default: the file name |
| `gainDb` | level, dB (0 = as recorded) |
| `fadeIn`, `fadeOut` | seconds |
| `trimIn`, `trimOut` | seconds cut from the start / end |
| `pan` | -1 … 1 |
| `loop` | true/false |
| `beatGrid` | `"auto"` (from the analysis) or `{"bpm": 126, "firstBeat": 0.449, "beatsPerBar": 4}` — shows bars in quewi's editor and enables its Fill with beats. Only when the analysis says the tempo is **steady**: a constant grid drifts against a song whose tempo moves (the build warns). Leave it out otherwise; fills still land on the beats heard |
| `triggers` | lighting triggers, below |

**Memo cue** (`"type": "memo"`): just `number`, `name`, `notes` — a
reminder or a stand-by point in the list ("House to half").

**Vamps** (quewi 1.3+): `"vamps": [...]` on an audio cue — performance-track
style vamps, repeats, holds, skips and markers, moved on in the show by
**Advance** (→):

```json
"vamps": [
  {"kind": "vamp",   "bars": "17-18", "name": "Vamp A", "exit": "end"},
  {"kind": "repeat", "bars": "33-36", "count": 2},
  {"kind": "hold",   "at": "bar 49", "name": "Fermata"},
  {"kind": "skip",   "start": "2:10", "end": "2:21"},
  {"kind": "marker", "at": "bar 57", "name": "Letter D"}
]
```

`bars` covers whole bars (like a fill); or `start`/`end` (`at` for a hold or
marker). `exit` for a vamp: `end` (end of the pass, default), `bar` (next
bar — needs a `beatGrid` on the cue), `now`. Vamps can't overlap. Put a
desk GO *after* a vamp, not inside it (triggers inside a vamp fire every
pass).

## Times

Anywhere a time goes: seconds (`83.5`), `"1:23.5"`, or with `--analysis`
`"bar 17"` / `"bar 17 beat 3"` (bars counted from the song's first
downbeat, as analyze_track.py printed them; beat may be fractional).

## Triggers

```json
{"at": "bar 27", "name": "Chorus 1", "do": "cue", "list": 1, "number": "12",
 "note": "full stage, saturated", "group": "Looks"}
```

- `at` (required), `name` (shows on the waveform), `note` (goes on the cue
  sheet only — the lighting designer's intent), `group` (quewi edits a group
  together), `enabled` (default true). A fill takes a `note` too.
- **Range**: add `"end"` and an `"exit"` action — the entry action at `at`,
  the exit at `end` (e.g. sub to 100 at `at`, back to 0 at `end`).

### Actions (`do`)

| `do` | needs | the desk does |
|---|---|---|
| `go` | — | GO on the main playback (next cue in its list) |
| `back` | — | Back |
| `stop` | — | Stop |
| `cue` | `number`, `list` (default 1) | go to that cue (Eos `Cue L/N Go`, MA `Goto`) |
| `goList` | `list` (fader page / MA sequence), `number` (fader) | GO on another cue list |
| `subLevel` | `number`, `level` 0–100 | set a sub |
| `subBump` | `number`, `hold` s (0.25) | press a sub's bump, release after `hold` |
| `faderLevel` | `number`, `list` (page), `level` | set a fader |
| `faderBump` | `number`, `list`, `hold` | press a fader's button |
| `macro` | `number` | fire a macro |
| `command` | `text` | type on the desk's command line (Eos, MA3) |

Cue / sub / macro numbers are strings so `"1.5"` keeps its decimal.

Raw alternatives (rarely needed): `{"at": …, "osc": {"address": "/eos/key/go_0", "args": "1", "host": "", "port": 0}}`
(empty host/port = the desk in quewi's settings), or
`{"at": …, "midi": {"type": "noteOn", "channel": 1, "data1": 60, "data2": 127, "port": ""}}`.

### Fills: one trigger per beat

```json
{"fill": {"bars": "72-87", "every": 2,
          "do": "subBump", "number": "1", "hold": 0.15,
          "name": "Pulse {n}", "group": "Big chorus pulse"}}
```

Where it runs — either:

- `"bars": "92-114"` — whole bars: from bar 92's downbeat to the last beat
  before bar 115 (the usual choice), or
- `"from"` / `"to"` — any times; `to` is **inclusive** and exact, so
  `"to": "bar 87"` stops *on* bar 87's downbeat (use `"bar 87 beat 4"` to
  include that bar, or `bars`).

`every`: every beat (1), every other (2), every bar (= beats per bar).
`beats`: `"tracked"` (default — the beats actually heard; right for live
recordings and anything that drifts) or `"grid"` (the cue's constant
`beatGrid`). The action can also go in `"action": {...}`. All fill
triggers share the group, so the user can move/edit them together in quewi.
