# quewi design review — 2026-07-17

A whole-app design review, following the theme-coherence fix (`fc3f64a`). This is a
findings report only; nothing was changed. Findings are ranked by how much they hurt
the operator in the booth, not by how easy they are to fix.

## Coverage (honest)

**Read deeply:** `Theme.h` / `Theme.cpp`, `quewi-dark.qss` (all 630 lines),
`quewi-light.qss` (first ~80 lines), `CueListView.cpp`, `TransportBar.cpp`,
`ActiveCuesPanel.cpp`, `MixView.cpp`, `MixGridModel.cpp`, `CartView.cpp` (the pad
painting), `Inspector.cpp` (structure, header, first ~250 lines, plus a full grep of
its styling calls), `PreferencesDialog.cpp` (first ~120 lines), `LiveAudioScope.cpp`,
and the theme-application path in `MainWindow.cpp` / `main.cpp`.

**Grep-sampled (colour usage, styling calls, button-box patterns — not read line-by-line):**
`WaveformWidget`, `TimelineCanvas`, `ParametricEqDialog`, `CompressorDialog`,
`EffectsRackWidget`, `NotificationsDialog`, `ShortcutsDialog`, `PreflightDialog`,
`ScriptViewer` / `ScriptPdfView`, `CommandPalette`, `CueListModel` (in `src/core`),
plus a repo-wide sweep for hardcoded hex / `QColor(...)` and `QPalette::Highlight`.

**Not opened at all:** `MediaImportDialog`, `PatchEditorDialog`, `WelcomeDialog`,
`AboutDialog`, `OscMonitor`, `StageView`, `ProjectionMappingDialog`, `CornerPinEditor`,
`AudioEditorWindow` (the window shell — its children were sampled), `VideoScrubber`,
`WhatsNewDialog`, `FindReplaceDialog`, `SpeakerPatchDialog`, `ScriptWindow`,
`PreflightDialog` layout, `NotificationsDialog` layout. Claims below about those files
rest on grep evidence only. I did not build and run the app; everything here is from
reading paint code and stylesheets, so pixel-level claims (e.g. exact Fusion default
colours) should be spot-checked on screen once.

---

## Findings, ranked by operator impact

### 1. Fusion's default blue leaks into show-critical surfaces — SEVERITY: HIGH

The app sets `QApplication::setStyle("Fusion")` (`src/app/main.cpp:140`) and themes
via QSS only. **No code anywhere calls `setPalette`.** So `QPalette::Highlight` is
Fusion's stock blue, and every place that reads it paints a foreign colour into the
warm-amber app:

- `src/ui/CueListView.cpp:750` — the drag-reorder drop indicator. The comment says
  "accent-coloured"; it is actually Fusion blue. This is the exact class of bug the
  bezel was: looks like a glitch, ships in the main surface.
- `src/ui/ScriptViewer.cpp:227` — the "accent" colour for script annotations.
- `src/ui/StageView.cpp:85`, `src/ui/CornerPinEditor.cpp:102` — same pattern.
- `CueListView.cpp:782` — the empty-state text reads `QPalette::PlaceholderText`,
  also unthemed (cool grey, not warm ink40).

Worse, **the brand-new Mix grid is entirely unstyled**: `quewi-dark.qss` has no
`QTableView` rule at all (it styles `QTreeView` and `QListWidget` only, plus
`QHeaderView::section` which the table's headers do inherit). So the Mix grid's
selected cell — which after GO-advance *is the standby indicator* — renders in
Fusion's default blue highlight, and its `setAlternatingRowColors(true)` stripes use
the default `AlternateBase`, not the warm `@bgRowAlt`. The most state-critical
affordance in the newest view is the least themed thing in the app.

**Direction:** add a `QTableView` block to `quewi-dark.qss` mirroring the
`QTreeView` rules, and replace the four `palette().color(QPalette::Highlight)` reads
with `Theme::tokens().accent` (or `outlineFocus`). Optionally also set a QPalette
from tokens at theme load as a safety net for anything else Qt draws natively.

### 2. MixView: the live cue is invisible — SEVERITY: HIGH

`MixView` tracks `m_liveCue` (`MixView.cpp:389`) — the cue currently applied to the
console — but **nothing ever paints it**. After `fireSelected()` advances the
selection (`MixView.cpp:398-400`), the cue that is ON the desk is the unmarked row
above the selection. The main cue list solves exactly this with the green running
wash; the Mix grid, where a wrong mix state means open mics in the house, gives the
operator nothing. Mid-show, "which cue is the desk actually on?" is unanswerable at
a glance.

**Direction:** give the live row the same treatment the main list gives running cues —
a `t.running` wash at low alpha via `BackgroundRole` (the model already has
`Theme::tokens()` in scope), and/or a left accent border. `MixGridModel` would need
to be told the live cue (a `setLiveCue(MixCue*)` slot from `MixView`).

### 3. The audio-editor suite is a different app visually — SEVERITY: HIGH (coherence)

Three custom-painted surfaces share a private cool blue-grey design language that
predates (or ignores) the warm theme, and between them they violate every line of
the stated direction:

- `WaveformWidget.cpp:21-28` — cool near-black bg `#16181D`, **blue** wave `#62B4FF`,
  **violet** fade overlays `#A88BFF` (the direction says no purple), yellow trim bars
  `#F2C94C`, cool grey text `#A8AEBA`.
- `TimelineCanvas.cpp:169-427` — an entire parallel palette: cool surfaces
  `QColor(28,30,38)` / `(32,35,44)`, blue-grey rulers and text, saturated red playhead
  `(255,60,60)`, selection **yellow** `(255,220,60)`, mute/solo chips `(220,80,80)` /
  `(220,180,30)`. None of it token-driven; none of it warm.
- `ParametricEqDialog.cpp:239-450` — its own cool background `#181C22` **with a
  gradient** (direction: no gradients), a **neon spectrum fill** red→orange→yellow→green
  (`.cpp:326-330`), pink and yellow band handles (`.cpp:40-42`), and a blue response
  curve `#A4C9FF`. This is the single most off-brand surface in the app.
- `EffectsRackWidget.cpp:32-36` — effect chips in bright blue / teal / **violet** /
  amber.

Meanwhile `CompressorDialog.cpp` is a model citizen — fully token-driven
(`tk.accent`, `tk.warn`, `tk.info`, `tk.running`), reading tokens at paint time. So
the discipline is provably achievable in exactly this kind of widget; the other three
just never got the pass. Opening the audio editor from the warm cue list is a visible
"different product" jump — the strongest "stapled together" signal in the app.

**Direction:** map the editor palette onto tokens: surfaces → `bgInverse`/`bgDeep`,
wave → `info` (already defined as the EQ-ish blue) or `ink60`, trim → `warn`, fades →
`accent` at alpha (kills the violet), playhead → `err`, grid/text → `ink40`/`ink60`.
The EQ's neon spectrum gradient should become a single quiet colour at low alpha —
CompressorDialog demonstrates the register. This is a real (day-scale) pass, not a
one-liner.

### 4. State colours are hardcoded in the model layer — high-contrast theme doesn't reach the cue list — SEVERITY: HIGH (for the users the HC theme exists for)

`src/core/CueListModel.cpp:341,360-364` hardcodes the running green `0x6FAE63`,
loaded blue `0x4F8EAF`, and both inks for the state dot and the running wash — with a
comment admitting it mirrors `Theme::tokens()` "without core depending on ui". The
same pastels are hardcoded in the VU meters (`CueListView.cpp:77-79`,
`ActiveCuesPanel.cpp:97-99,105` — plus the meter's background `0x1F1D1B` at `:77`)
and in `ScriptViewer.cpp:228-229` / `ScriptPdfView.cpp:184-186` /
`NotificationsDialog.cpp:24-26`.

Consequence: switch to **high-contrast** — the theme whose stated purpose is
"visually-impaired operators and badly-lit FOH positions" — and the running-cue
indicator in the main cue list *stays the muted mossy pastel*. The one colour that
theme most needs to change is the one it can't touch. Midnight/forest/synthwave break
identically, and in the light theme these dark-tuned washes sit on white rows.
Nobody has noticed because the hardcoded values match the default dark palette — which
is exactly how this class of drift stays invisible.

**Direction:** the model shouldn't own colours at all. Either have the views/delegates
translate a state role into `Theme::tokens()` colours, or inject a small colour
provider into `CueListModel`. The meters are trivial: read `Theme::tokens()` in
`paintEvent` like `SeekBar` (same file!) already does.

### 5. The light theme is a different design language, and half of the app ignores it — SEVERITY: HIGH if light is a real offering, LOW if it's vestigial

Two independent problems:

1. `quewi-light.qss` is not the warm direction inverted — it's a **cool** blue-grey
   design: `#F4F5F8` surfaces, `#2563EB` blue accent, `#CFE2FF` selection, and even a
   different font stack (Inter, vs the dark theme's IBM Plex Sans — `quewi-light.qss:14`
   vs `quewi-dark.qss:27`). Same app, two brands.
2. `Theme::load("quewi-light")` falls through `tokensForName` to the **warm-dark
   tokens** (`Theme.cpp:121`). So in light mode, every C++-painted widget — cart pads,
   seek bars, peak meters, transport chips, the Inspector empty state, MixView status
   colours — paints *dark-theme* colours onto light surfaces. The empty cart pad
   literally paints white-alpha ink (`CartView.cpp:184-189`) that vanishes on white.

**Direction:** decide the light theme's status. If it's real: define a light token
set (warm paper, same amber accent, same font) and let it flow through the same
substitution pipeline. If it's not: mark it experimental in the picker or remove it.
The current state is the worst option — it looks shipped and behaves broken.

### 6. Theme switching leaves stale colours everywhere tokens are captured at construction — SEVERITY: MEDIUM

`MainWindow::applyTheme` (`MainWindow.cpp:1078-1095`) swaps the QSS live — good. But
many widgets bake `Theme::tokens()` values into inline stylesheets or member colours
**once, at construction**: TransportBar's Fade/Panic chips (`TransportBar.cpp:83-100`),
Inspector's reset buttons (`Inspector.cpp:93-116`) and empty state (`:169-190`),
MixView's status label and warning banner (`MixView.cpp:70,116-119`),
ActiveCuesPanel row labels (`ActiveCuesPanel.cpp:255-262`), CompressorDialog's panel
background (`CompressorDialog.cpp:37`), PreferencesDialog hint labels (`:110`).
Switch palettes mid-session and the app runs a mixture of the old and new theme until
restart. Widgets that read tokens in `paintEvent` (SeekBar, CompressorDialog's canvas)
retheme instantly; the two patterns coexist file-by-file with no rule.

Special case: the transport **Pause** button doesn't even use tokens — it hardcodes
the warm hexes directly (`TransportBar.cpp:69-72`), so it's wrong in all four
alternate palettes even at startup, while its siblings Fade All / Panic three lines
down do it correctly.

**Direction:** adopt one rule — *painted widgets read tokens at paint time; inline
stylesheets built from tokens must be rebuilt on a theme-changed signal* (or just
re-set in a `changeEvent(StyleChange)`). Fixing Pause to use tokens is a one-liner.

### 7. MixView is functionally sound but hasn't joined the app's design system — SEVERITY: MEDIUM (beyond items 1 and 2)

Called out separately since it was requested. What the model gets *right* deserves
saying: the change-vocabulary (assigned/modified/removed/unchanged mapped to
ink100/warn/ink40/ink60, backgrounds only where something changes) is token-driven,
restrained, and better-reasoned than most of the older code. The problems are around
it:

- **Alternating rows contradict the app's own argued position.** `MixView.cpp:128`
  turns banding on; `CueListView.cpp:137-142` turns it off with a paragraph explaining
  why banding is noisy and QLab-style uniform rows + hairline dividers are the house
  style. The two cue lists in this app disagree about what a cue list looks like.
- **GO sits in an editing toolbar.** The Mix GO (`MixView.cpp:91-94`) is a standard
  26 px button, first in a row with "Add cue" and "Delete cue". The app's design
  language says GO is a 64 px state-coloured hero. Here the show-firing control is
  visually identical to, and one button away from, *Delete cue*. Even keeping it
  compact, it should be visually distinct (state colour, weight) and not adjacent to
  destructive editing actions.
- **`Removed` is invisible.** `changeFor` classifies a cell whose mics all left as
  `Removed` (`MixGridModel.cpp:121`) and colours its *foreground* ink40 — but the cell
  text is now empty (`cellText` returns nothing), so the signal renders as blank.
  A DCA silently emptying between cues is exactly what an operator scanning ahead
  wants to see. TheatreMix shows departures; here they don't exist visually. Consider
  ghosting the departed names (strikethrough/ink40) in the cell.
- **Cue number format differs**: `'g', 6` (`MixGridModel.cpp:145`) vs the `'f', 2`
  used by the cue list, transport NEXT label, and ActiveCues label. "1.5" in Mix,
  "1.50" everywhere else.
- Small: the warning banner (a good idea, well-reasoned) is inline-styled ad hoc
  (`MixView.cpp:116-119`); fine for now, but it's another construction-time token bake
  (see item 6).

### 8. Two meanings of amber, two colours of progress — SEVERITY: LOW-MEDIUM

- `accent` (#C58B4A) and `warn` (#D7A24E) are nearly the same colour, and the code
  uses them interchangeably: the ActiveCues seek bar fills with `tk.warn`
  (`ActiveCuesPanel.cpp:182` — a progress fill, not a warning), while `QProgressBar`
  fills with `@loaded` blue (`quewi-dark.qss:624`). So progress is amber in one panel
  and blue in another, and `warn` is being used as "second accent". Harmless in the
  default palette; in any palette where warn diverges from accent, seek bars turn
  warning-coloured. Pick one progress colour (loaded-blue or accent) and reserve
  `warn` for warnings.
- Near-miss token duplicates that will drift: `PreflightDialog.cpp:237` warn-yellow
  `0xF2C94C` vs token `warnBright 0xE8C861`; `WaveformWidget.cpp:187` red `0xFF5A5A`
  duplicating `errBright`; `ShortcutsDialog.cpp:73` (`0xA8AEBA`) and
  `CommandPalette.cpp:78` (`0x4A4F5A`) — both *cool* greys doing ink60/ink40's job;
  `CartView.cpp:52` cool near-white `#F2F5F8` as pad ink where warm `ink100` exists.

### 9. Radius / ornament drift outside the documented scale — SEVERITY: LOW

The scale says 3/4/2 px with GO's 6 px as the sole exception. Currently also at 6 px:
Pause, Fade All, Panic (`TransportBar.cpp:68,82,95` — 44 px tall, arguably the same
"hero cluster" logic as GO, but undocumented). The Inspector reset button is 4 px on a
control (`Inspector.cpp:99` — should be 3). The cart pads are 11 px with vertical
gradients and a glow ring while playing (`CartView.cpp:166,199-209`) — directly
against "no gradients, no glow", though the file argues the soundboard should read
"like a Launchpad". That's a defensible idiom for that surface, but right now it's an
unwritten exception, which is how the last nine radii happened. One-line fix for the
reset button; the rest is a documentation decision: either write the exceptions into
`Theme.h`'s comment block (as GO is) or align them.

### 10. Fragile running-row detection in the cue list delegate — SEVERITY: LOW (today), latent

`CueRowDelegate` decides "is this the running tint?" by heuristic:
`alpha >= 60 && green > red && green > blue` (`CueListView.cpp:103`). It currently
works only because the running wash is alphaF 0.28 (≈71) and user tints are alphaF
0.22 (≈56) — a 4-alpha-point margin (`CueListModel.cpp:342,348`). Nudge either
constant, or retheme running to a non-green (midnight's running is teal-green, fine;
a future palette may not be), and green user-coloured cues start reading as running —
a genuinely dangerous mislabel mid-show. A dedicated custom role
(`IsRunningRole`) would make this exact instead of inferred. Also note
`ActiveCuesPanel::refresh` sets the whole panel `setVisible(false)` when idle
(`ActiveCuesPanel.cpp:455`) — first cue fired mid-show causes a layout reflow of
whatever shares its splitter. Worth checking on screen whether the pop-in moves the
cue list.

### What's genuinely good (so it doesn't get "fixed")

- The dark QSS is disciplined and unusually well-commented; the 26 px control-height
  standardisation and the focus-ring padding compensation are exactly right.
- Dialog conventions are consistent: `QGroupBox` + `QFormLayout` bodies,
  `QDialogButtonBox` with Close, right-aligned labels. The Inspector, feared to be
  incoherent at 2332 lines, is actually structurally uniform — it's big, not messy.
- Micro-caps header idiom (NEXT / ACTIVE / group titles) is applied consistently.
- The empty states (cue list, inspector, cart pads) teach shortcuts instead of
  sitting blank — nice operator-first touch.
- MixGridModel's change-colour reasoning and MixView's warning-banner policy
  ("things that make the tool silently wrong get a banner") are the right instincts.

---

## Questioning the direction itself (kept separate from defects)

1. **`accent` vs `warn` being the same hue family is a semantic collision.** The
   direction says "one accent: amber" and also "amber = warn". The result is that a
   focus ring, a selected row's edge, and a warning read as the same colour. Within
   the warm-restrained brief there's room to keep amber as accent and let warn be
   distinguishably hotter (the `warnBright` yellow already exists). This is a palette
   question, so it's flagged here rather than as a defect.
2. **Synthwave contradicts the stated direction** ("no purple, no neon") on its face.
   If the fun palettes are exempt, one sentence in `Theme.h` saying so would stop the
   direction statement from being falsifiable by the theme picker.
3. **Is the cool-toned audio editor a deliberate "different room"?** DAWs are often
   cool/dark relative to their hosts. If that's the intent, it should be declared and
   tokenised (an `editor*` token group) rather than hardcoded; if not, item 3 above
   applies. I'd argue against the exception — the CompressorDialog proves the warm
   palette handles technical surfaces fine — but it's a legitimate call either way.

---

## What I'd fix first, if it were my call

1. **The Fusion-blue leaks (finding 1).** A `QTableView` QSS block plus four
   one-line `QPalette::Highlight` → token replacements. Smallest effort, largest
   removal of "this app is flaky" signal, and it fixes the Mix grid's selection —
   which is currently the app's most visible foreign element.
2. **Live-cue marking in MixView (finding 2).** It's the one place an operator can't
   answer "what is the desk doing right now" at a glance, and it's cheap: the model
   already has the tokens and the pattern exists in CueListModel.
3. **Tokenise the state colours in CueListModel and the meters (finding 4).** Until
   this lands, the high-contrast theme is a false promise exactly where it matters
   most. This also de-fuses the delegate heuristic (finding 10) if done via an
   explicit role.
4. **The audio-editor retheme (finding 3).** Biggest single coherence win. Use
   CompressorDialog as the reference implementation.
5. **Decide the light theme's fate (finding 5).** Not because light mode matters in
   a dark booth — it mostly doesn't — but because a shipped theme that visibly
   half-works undermines trust in everything else.

---

## Fusion fall-through pass (2026-07-17)

Follow-up acting on the findings above. Premise confirmed: every "beveled /
foreign / flaky" artifact traced to one root cause — a control or painted widget
the QSS never named, falling through to Fusion's stock rendering. Two sub-species,
two fixes.

### The root-cause fix: a global QPalette (`Theme::palette()`)

`main()` set Fusion and a QSS but never a palette, so every
`palette().color(QPalette::…)` read in a QPainter widget resolved to Fusion's
defaults — stock blue Highlight, cool greys. `Theme::load()` now derives a
QPalette from the active token set and applies it application-wide
(`QApplication::setPalette`) every time a theme loads, so runtime theme switches
re-theme painted widgets with zero per-widget plumbing. Role mapping:
Highlight→accent, Base→bgPanel, AlternateBase→bgRow, Text/WindowText→ink100,
PlaceholderText→ink40, Mid→outline, Midlight→divider, Button→bgInteractive,
plus a full Disabled group.

Painted-widget sites this resolves in one stroke, none of which needed editing:

- `CueListView` drag-drop indicator (was Fusion blue mid-drag) and empty-state hint
- `CornerPinEditor` — stage fill, edges, handles, snap halo (five roles)
- `StageView` — disc, rings, crosshair, elevated-speaker boxes
- `ScriptViewer` gutter accent
- Fusion's own drawing of anything the QSS skips (combo arrows, frame shading)
  now shades from theme colours instead of stock grey/blue

A light token set (`lightTokens()`, mirroring quewi-light.qss's hexes) was added
so the palette is correct under the light theme too — previously
`Theme::tokens()` silently returned *dark* tokens under light, which would have
made a global palette a regression there. All hexes already existed in the light
QSS or dark tokens; the two accent variants are `darker()`/`lighter()` derived.

### QSS coverage audit (sub-species A)

Swept every widget type in use against what the two stylesheets name. Fixed:

- **QRadioButton** (media import's Audio/Video choice) — completely unstyled;
  Fusion indicator, blue dot. Now the round variant of the checkbox vocabulary.
- **QAbstractScrollArea::corner** — the raised grey chip where two scrollbars
  meet. Flattened to the track colour (dark + light).
- **QDockWidget::title** (Inspector dock) — Fusion's raised strip was the last
  visibly foreign band in the main window. Now a calm panel band with a hairline,
  same vocabulary as QHeaderView (dark + light).
- **QScrollArea** — default StyledPanel frame drawn by the base style; killed
  app-wide (Inspector already opted out locally; others inline-styled around it).
- **QMenu::indicator** — checkable menu items (View → Inspector, theme picker)
  drew Fusion's stock glyph. Now a mini checkbox: outline box, accent fill.

Audited and found already covered or not showing: QTabBar/pane, QCheckBox
indicators + disabled states, QProgressBar, QSlider grooves/handles, QToolTip,
QMenu separators, QSplitter handles, QToolButton, QKeySequenceEdit (styled via
its inner QLineEdit), QTableWidget (matches the QTableView rules by inheritance),
QToolBar (audio-editor-local, inline-styled).

### Token reads where the palette can't express it

- **ScriptViewer** — running/next gutter colours and line highlights were the
  warm-dark hexes baked in; the other four palettes never reached them. Now
  `Theme::tokens().running/.warn/.accent` with per-use alpha.
- **WaveformWidget** — the audio editor's cool-blue palette, but this widget
  lives in the *Inspector*, so it was a patch of a different product embedded in
  the warm pane. Its file-static constants are now theme-token reads resolved at
  paint time (bg→bgDeep, wave→info — the old 0x62B4FF literal *was* the info
  token verbatim — trim→warnBright, error→errBright verbatim, playhead→ink100).
  The fade overlay moved from off-direction purple onto the accent.

### Left for a real design pass

- **TimelineCanvas / AudioEditorWindow** (the multitrack editor proper) — ~25
  hardcoded cool-blue colours including gradients, region tints, mute/solo
  states, and an inline-styled toolbar. That is finding 3's full retheme, a
  design job, not a mechanical substitution; doing half of it (canvas but not
  chrome) would leave the editor internally inconsistent. The editor is a
  separate window, so the private palette reads as "different room" rather than
  a defect patch in the meantime.
- **Light theme's Fusion indicators** (checkbox/radio) — deliberately kept;
  Fusion's light rendering is native-looking there, and light "relies on Fusion
  for widgets we don't override" by declared design. With the new light palette
  those indicators now at least pick up the light accent for their checked state.

Verified: clean build, 18/18 test suites, `--selftest` exit 0. Not visually
verified in a running session (window-management not available in this pass):
dock title bar under all five palettes, menu indicator spacing.
