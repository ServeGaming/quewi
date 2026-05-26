# `.quewi` file format

Show files are **SQLite 3 databases**. A `.quewi` file is a
single self-contained database — you can open one in any SQLite
browser to inspect cues, patches, scripts, and undo history.

---

## Why SQLite?

- **One file** — copy / version-control / email a show as a
  single artefact.
- **Atomic writes** — SQLite's WAL journal protects against
  partial writes if quewi crashes mid-save.
- **Fast** — load a 5,000-cue show in milliseconds; SQL queries
  for find/filter run instantly without an index pass.
- **Inspectable** — DB Browser for SQLite, the `sqlite3` CLI, or
  any other SQLite tool can read the file.
- **Boring** — a 25-year-old, stable format used by thousands of
  apps. Won't disappear.

---

## Schema overview

The high-level tables:

| Table | Holds |
|---|---|
| `meta` | Schema version, app version, workspace UUID, last-saved-by-version |
| `cue_lists` | One row per cue list, ordered by `index_in_workspace` |
| `cues` | One row per cue, ordered within their cue list. Includes the cue's type, common fields, and a JSON payload of type-specific fields |
| `patches` | Audio outputs, DMX universes, OSC destinations, MIDI ports |
| `speaker_arrays` | Speaker positions for object-audio routing |
| `script` | Stage manager's annotated script (one per workspace, optional) |
| `cart` | Cart-grid layout — cell index → cue UUID mappings |
| `effects` | Per-cue effect chains (EQ / Compressor / Reverb / Delay) |

---

## The `cues` table

The largest table. Common columns plus a `payload` JSON blob for
type-specific fields.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID, primary key |
| `cue_list_id` | TEXT | FK → `cue_lists.id` |
| `row` | INTEGER | Ordering within the cue list |
| `type` | TEXT | `"audio"`, `"video"`, `"light"`, etc. — see [OSC field reference](../osc-control/field-reference.md) for the type-key list |
| `number` | REAL | Cue number (e.g. 3.5) |
| `name` | TEXT | Display name |
| `pre_wait` | REAL | Seconds |
| `post_wait` | REAL | Seconds |
| `continue_mode` | INTEGER | 0=DoNot, 1=AutoContinue, 2=AutoFollow |
| `notes` | TEXT | Free-form notes |
| `armed` | INTEGER | 0/1 |
| `color` | TEXT | `#AARRGGBB` hex (optional) |
| `payload` | TEXT | JSON object of type-specific fields |

The `payload` JSON columns match the OSC field names exactly. An
AudioCue's `payload` looks like:

```json
{
  "filePath":       "S:/show/01-overture.wav",
  "gainDb":         0.0,
  "fadeInSeconds":  2.0,
  "trimInSeconds":  0.0,
  "pan":            0.0,
  "loop":           false,
  "outputDeviceId": "foh-stereo"
}
```

Same shape as what `/quewi/query/cue` returns over OSC.

---

## Versioning

The `meta` table holds `schema_version` (currently `1`) and
`quewi_version` (the app version that last saved the file).

- **Forward compatibility** — a newer quewi can open older files;
  any new schema columns get default values.
- **Backward compatibility** — an older quewi opening a file
  saved by a newer version checks `schema_version`. If the
  newer version added fields the older doesn't understand,
  quewi warns ("this file was saved by a newer version; some
  data may be ignored") rather than refusing.

The version number only bumps on **breaking** schema changes
(removed columns, retyped columns). Additive changes (new
columns with defaults, new tables) stay on the same version.

---

## Inspecting a `.quewi` file

```sh
# List the tables
sqlite3 myshow.quewi ".tables"

# Show the schema
sqlite3 myshow.quewi ".schema cues"

# Dump every cue's name and type
sqlite3 myshow.quewi "SELECT number, name, type FROM cues ORDER BY row"

# Show the JSON payload for cue 5
sqlite3 myshow.quewi "SELECT payload FROM cues WHERE number = 5"
```

[DB Browser for SQLite](https://sqlitebrowser.org/) is a free
GUI that does the same.

---

## Don't edit a `.quewi` file while quewi has it open

SQLite handles concurrent reads fine, but two writers (quewi
itself + an external tool) corrupt the journal. Close the
workspace in quewi before opening the file in an external
tool.

---

## Backup & version control

`.quewi` files are small (typically under 100 KB for a real
show; cue counts, not media, drive size). They're well-suited
to Git:

```sh
git init my-show
cd my-show
cp ../myshow.quewi .
git add myshow.quewi
git commit -m "Initial cue list"
```

Diffs aren't human-readable (binary format), but Git tracks
changes fine and you can roll back to any earlier version.

For shows with embedded media references, consider also
committing the audio / video files. Most theatres put `.quewi`
+ media + show notes in a shared show folder under Git LFS or
Dropbox-style sync.

---

## Format stability promise

The on-disk format is considered stable from v1.0 onward. Files
saved by v1.0+ will open in every subsequent release. Anything
quewi can save, it can re-open.

Pre-1.0 `.quewi` files (the v0.9.x series) may not be fully
forward-compatible — we'll do our best, but the schema is
allowed to break until 1.0. Save a backup of any pre-1.0 show
before upgrading to a major version.
