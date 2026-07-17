# Work plan

Living list. Written 2026-07-17. Ordered by "would this embarrass us in front of
an operator", not by effort.

## In flight

| What | Who | State |
|---|---|---|
| Theme coherence pass (the "bezel / buggy look" report) | Fable 5, background | running |
| Detach-to-window respects list kind | done, local commit | ✅ |

## Done this session

- **Detach-to-window handed you a cue table whatever you detached.** It built a
  `CueListModel` + `CueListView` unconditionally with no check on
  `CueList::Kind`. Now dispatches: Mix → `MixView`, Soundboard → `CartView`,
  Normal → `CueListView`. **This was pre-existing** — detaching a soundboard has
  been broken the same way since soundboard tabs landed; the mix grid just made
  it visible.

## The theme problem (handed to Fable 5)

Diagnosed, not guessed:

1. **Eight different corner radii** — 1, 2, 3, 4, 5, 6, 8, 10, 12 px — where
   `Theme.h`'s own comment says *"Soft 3 px corner radii on controls, 4 px on
   panels."* The QSS drifted from its stated design and nothing lines up. Near
   misses everywhere is what reads as "buggy" to the eye.
2. **2px light borders around every control.** `QPushButton`, the
   `QLineEdit`/`QSpinBox`/`QComboBox` group and `QGroupBox` all carry
   `border: 2px solid @outline`. `@outline` (`#4A443D`) is **lighter than both**
   the fill (`#34302C`) and the panel behind it (`#262422`) — so every control
   is outlined in a light rectangle. That's the "bezel": embossed Windows-95
   chrome. At fractional DPI the 2px stroke around a 5–6px radius aliases at the
   corners, which is very plausibly the literal artifact being seen.

Brief: normalise to a 3-value radius scale, drop resting borders to 1px, keep
the 2px amber focus ring (it's an accessibility affordance and the one place a
loud border belongs), watch for focus-time layout jumps, do the light theme too,
and fix the lying comment in `Theme.h` — that comment being wrong is how this
drifted in the first place.

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
