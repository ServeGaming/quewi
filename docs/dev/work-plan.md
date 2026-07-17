# Work plan

Living list. Written 2026-07-17. Ordered by "would this embarrass us in front of
an operator", not by effort.

## Done this session

- **Detach-to-window handed you a cue table whatever you detached.** It built a
  `CueListModel` + `CueListView` unconditionally with no check on
  `CueList::Kind`. Now dispatches: Mix → `MixView`, Soundboard → `CartView`,
  Normal → `CueListView`. **This was pre-existing** — detaching a soundboard has
  been broken the same way since soundboard tabs landed; the mix grid just made
  it visible.
- **The theme "bezel / buggy look"** — fixed by Fable 5 in `fc3f64a`. See below.

## The theme problem — fixed, and what it actually was

Diagnosed, not guessed:

1. **Nine different corner radii** — 1, 2, 3, 4, 5, 6, 8, 10, 12 px — where
   `Theme.h`'s own comment said *"Soft 3 px corner radii on controls, 4 px on
   panels."* The QSS had drifted from its stated design and nothing lined up.
   Near-misses everywhere is what reads as "buggy" to the eye.
2. **2px light borders around every control.** `QPushButton`, the
   `QLineEdit`/`QSpinBox`/`QComboBox` group and `QGroupBox` all carried
   `border: 2px solid @outline`. `@outline` (`#4A443D`) is **lighter than both**
   the fill (`#34302C`) and the panel behind it (`#262422`) — so every control
   was outlined in a light rectangle. That's the bezel: embossed Windows-95
   chrome. At fractional DPI a 2px stroke around a 5–6px radius aliases at the
   corners, which is plausibly the literal artifact that was being seen.

**Outcome:** radii 9 distinct values → 5; resting 2px borders 5 → 2, and both
survivors are `:focus` rules. Every focus rule sheds exactly 1px of padding per
side as its border grows, so the outer box and text baseline don't move — a
control that shifts 1px when you tab into it is its own kind of "buggy look".

**Verified independently, not taken on trust** (the safety classifier was down
when the agent's work was reviewed): commit scope clean, braces balanced, no
invented `@tokens`, padding maths checked by hand, 18 suites green,
`--selftest` 0.

**`@outline` was deliberately NOT darkened, and that was the right call.** It
doubles as a *fill* — slider sub-page/add-page and scrollbar handles (4 sites) —
so darkening it would dim legibility-critical elements to fix something the 1px
weight already fixes. It's also shared by **five palettes** (dark,
high-contrast, midnight, forest, synthwave all use `quewi-dark.qss` and only
swap tokens), so a hue change would need re-tuning per palette while the 1px
change benefits all five for free.

**Known exception:** the GO button keeps a 6px radius (2× the control scale) —
at 64–72px tall, 3px reads as a sharp corner. Documented in both themes and in
`Theme.h`.

### Theme debt not fixed

- **The light theme is hardcoded hex, no `@tokens`.** Radii were normalised and
  it was already 1px-bordered, but true dark/light parity means tokenising it.
  Out of scope for a restraint pass; worth doing before anyone relies on light
  mode.
- **QSS radii don't scale with DPI**, so at 150% the 3px/4px distinction is
  subtle. Inherent to Qt stylesheets, not fixable in the theme.

## Next, in order

1. **Drive the mix grid in the real app.** Everything below the UI is tested,
   but the grid itself has only been proven by unit tests and a compile. Not the
   same thing. Create a list, add cues, type an assignment, watch the
   highlighting. *Nothing else on this list matters if the grid doesn't work.*
2. **The Windows updater.** Still outstanding from before the mix work: user
   reported "download bar, then quewi just closes, nothing installs" on 0.9.103.
   Client step-logging shipped; needs a user run to produce
   `%APPDATA%/quewi/update-client.log`. **Blocked on the user, not on us.**
3. **Ship what's local.** Nine-plus commits sitting unpushed, including a
   `CartGrid` crash fix from the pre-1.0 audit that never got committed until
   this session.
4. **DM7 hardware session.** `tools/dm7_probe.py <IP>`. Blocked on the user
   being at the desk. Settles: `prminfo` self-description (retires the
   stale-table error class permanently), PEQ gain scaling (three sources
   disagree 1 vs 10 vs 100 — blocks phase 4 EQ), mute-group polarity
   (**undocumented; wrong guess mutes the cast mid-show**), and whether dynamics
   exist on current firmware.
5. **Channel/ensemble editor.** `MixShow` has channels, actors, backups and
   ensembles, all persisted — with no UI. Right now the only way to name a mic
   is to hand-edit the show file. The grid is unusable without this, so it's
   really part of phase 1 rather than a nice-to-have.
6. **Console patch editor.** `PatchManager::Category::MixingConsole` exists in
   the model and is deliberately **not** in `PatchEditorDialog`'s tab list,
   because that dialog switches on category without a default and would open an
   empty tab. Add the field editor and the tab together.

## Known-and-deliberate, not bugs

- **`quewi_ui` references `GoEngine`, which lives in the app target.** It only
  links because the app links both. `test_mix_grid` therefore compiles
  `MixGridModel.cpp` + `Theme.cpp` directly rather than linking `quewi_ui`. A
  real layering wart; not worth a refactor today, but it'll bite the next test
  that wants a UI class.
- **`MixShow::setDcaCount` doesn't prune assignments above the new count.**
  Lowering the count on a mis-click would otherwise destroy programming that
  raising it back can't recover.
- **No DM7 mute-group helper in `Dm7Value`.** Polarity is unverified; it doesn't
  get a convenient wrapper until hardware says.
- **moc bug**: moc treats `\"` as an escape *inside* raw string literals, emits
  an empty `.moc`, and the only symptom is unresolved `metaObject` symbols.
  Documented in `tests/test_dm7_value.cpp`. Don't reach for `R"(...)"` with
  backslashes in a `Q_OBJECT` file.
