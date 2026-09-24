# Soundboard

The soundboard is a grid of pads, each firing one sound. It's for the sounds
that don't belong in the running order: doorbells, phone rings, applause
sweeteners, spot effects the stage manager calls on the fly.

Open it with **View → Soundboard** (`Mod + Shift + C`). It gets its own tab
next to your cue lists, and it's saved with the show.

---

## Putting sounds on pads

- **Drag audio files** from Explorer / Finder onto a pad, or
- **double-click an empty pad** to pick a file.

Click a pad to fire it. A playing pad glows until the sound ends. **Stop All**
stops everything the soundboard started.

**Resize…** sets the grid: up to 16 rows × 12 columns. Shrinking it
removes the pads that no longer fit, on every layer. quewi tells you how
many and asks first, because this can't be undone.

**Output** routes the whole soundboard to one audio device, separate from the
cue list if you want (e.g. a stream mix, or a second interface).

---

## Customising a pad

Right-click a pad → **Customise pad…** (or turn on **Edit Layout** and click
it) to set:

- **Label**: defaults to the cue's name.
- **Colour**: a swatch or a custom colour.
- **Keybind**: a key that fires the pad (see below).
- **MIDI note**: type a note number, or press **Learn** and hit a pad on
  your MIDI controller.

**Unbind cue** empties the pad. Audio pads also have **Open in audio
editor…**.

---

## Keybinds

*New in 1.0.1.* Every pad can have its own key.

1. Right-click the pad → **Set keybind…**
2. Press the key. `Esc` cancels.

quewi won't let two pads on a layer share a key. If the key is already on
another pad, it asks whether to move it. It also refuses keys quewi itself
uses, like `Space` for GO, so a pad can never steal the transport. Letters, numbers and
F-keys work well.

To change or remove a key, right-click the pad → **Change keybind…** /
**Clear keybind**. Hover a pad to see its key.

### Where the keys work

The **Keys:** menu in the soundboard's toolbar sets where pad keys are
listened for:

| Setting | Pad keys fire… |
|---|---|
| **Board only** | while the soundboard is on screen |
| **Anywhere in quewi** | from any page, e.g. while you run the cue list |
| **System-wide** | even while another app (a game, Discord, OBS, a browser) has focus |

**System-wide** is the default on Windows. It works like Voicemod or a
Stream Deck: quewi can sit in the background and still fire pads. The other
app **still receives the key too**, so choose keys you don't type there:
`F13`–`F24`, the numpad, or `Ctrl + Alt` combinations are good picks.

!!! note "Platform support"
    System-wide keys are Windows-only for now. On macOS and Linux the menu
    offers Board only and Anywhere in quewi.

The setting is per computer, not per show.

---

## Layers

A soundboard can hold several pages of pads, called **layers** (Act 1, Act 2,
spot FX…). Only the visible layer fires, keys included.

- **+ Add layer** creates one.
- Click a layer's tab to switch. Right-click it to **Rename** or **Delete**.
- Layers can also be switched over OSC.

Each layer keeps its own layout and keybinds, so the same key can do
different things on different layers.
