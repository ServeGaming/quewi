# Command menu

One place to reach everything by keyboard: every menu action, the cues in
the list on screen, the other cue lists, recent shows, Preferences pages and
themes. It opens as a floating panel over the main window — type, pick,
done — and it has a **leader key**, so the most common jumps are a tap and a
letter away.

Two ways in:

| | Opens | Default |
|---|---|---|
| **Search** | A search field and a ranked list | `Ctrl + K` |
| **Key menu** | A grid of one-letter choices by category | tap **`** (backtick) — the *leader* |

Both are the same panel; Backspace on an empty search takes you to the key
menu, and typing anything in the key menu that isn't one of its letters
drops into the search.

## Search (`Ctrl + K`)

Start typing. Results update as you go and are ranked the way you'd expect
from a launcher: things that *start* with what you typed first, then
acronyms (`na` → **N**ew **A**udio), then words that start with it, then
anything containing it, then letters in order with gaps. Things you've picked
before get a nudge, and with nothing typed the list starts with your recent
picks.

- `↑` `↓` move, `↵` runs the selected result, `Esc` closes.
- The selected result's description sits at the bottom of the panel; its
  shortcut (if it has one) is shown on the right, so the menu doubles as a
  way to learn the keys.
- **Cues**: type a number or a name. `12` finds cue 12 before 112; `defy`
  finds *Defying Gravity*. `↵` **stands the cue by** — it becomes the next
  to GO. Nothing fires. `Ctrl + ↵` opens an audio or video cue in its editor.
- **Lists** switch the tab; **recent shows** open; **Preferences** pages open
  Preferences on that page; **themes** switch live.
- `Ctrl + P` pins the selected result to a leader chord (below).

## The key menu (the leader)

Tap **`** (the backtick, top-left of the keyboard) and the key menu opens:
a grid of categories, each with one letter.

```
C  Cue ▸        L  Lights ▸     S  Show ▸       G  Go to ▸
F  File ▸       E  Edit ▸       V  View ▸       I  List ▸
T  Tools ▸      P  Preferences ▸                H  Help ▸
```

Press a letter to go into a category, then the letter of the thing you
want. The letters are the menus' underlined accelerators wherever
possible, so they match what you already know from the menu bar:

- **C** then **A** — new Audio cue; **C** **L** — new Light cue; **C** **W** — new Wait.
- **L** then **D** — Lighting Desk…; **L** **A** — arm / disarm lighting triggers;
  **L** **P** — the Lighting panel.
- **S** then **M** — Show Mode; **S** **P** — Pre-flight; **S** **O** — OSC Monitor.
- **G** then **L** — switch list; **G** **R** — a recent show; **G** **C** — find a cue.
- **V** then **T** then a letter — a theme. **P** then a letter — a Preferences page.

`Backspace` goes up a level, `Esc` closes, and typing anything that isn't
one of the letters on screen (a cue number, say) drops you into the search
with it. `Space` does nothing here — it's GO everywhere else, and the panel
never fires anything.

Letters are remembered between runs so they never move on you. If two
items in a category want the same letter, the second takes another letter
from its name; **Preferences → Command menu → Reset mnemonics** re-derives
them all.

### Chords: hold the leader

Hold **`** and press a letter to skip the first step: **`** + **C** opens the
Cue category directly, **`** + **L** Lights, **`** + **S** Show, and so on for every
top-level letter.

**Pin anything to a chord.** In the search, select a result, press `Ctrl + P`,
then the letter: **`** + that letter now runs it straight away, without
opening anything. Pins win over the category letters while the leader is
held (the category is still one tap and a letter away). Pins are listed, and
removable, in **Preferences → Command menu**; `Ctrl + P` then `Backspace`
unpins from the search.

### Choosing the leader

The default is the backtick because it's a single key nothing else in
quewi uses, it sits in the top corner well away from `Space` (GO) and
`Esc` (Panic), and holding it for a chord is natural. If your keyboard
makes it awkward, change it in **Preferences → Command menu** (or in
**Tools → Keyboard shortcuts**, where it's *Command menu leader*): `Ctrl +
Space`, `F12`, `Pause`, `Scroll Lock`, `Insert` or the Menu key are offered.

The leader only works in the main window, and only when you're not typing:
in a text field the key just types its character.

## Safety

- **GO, Panic, Pause and Fade All are not in the menu.** The panel can't
  fire a cue: the only cue action is *stand by*, which selects it.
- `Space` and `Esc` do nothing to the show while the panel is open —
  `Esc` closes it.
- In **Show Mode** only run-safe items appear: no File, Edit, Cue or List
  actions, no list switching, no opening shows or Preferences, and cues
  can be stood by but not opened in an editor. What's disabled by the lock
  doesn't show up.

## Settings

**Preferences → Command menu** has the leader key, the chords switch, the
pinned chords, *Reset mnemonics* and *Forget recent picks*. `Ctrl + K` and
the leader are both rebindable in **Tools → Keyboard shortcuts**.
