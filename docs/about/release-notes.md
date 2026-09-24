# Release notes

## 1.0.2 (September 2026)

Effects presets, four new effects, sound effects straight from YouTube onto
the soundboard, and an updater that finishes the job.

**Updating from 1.0.1:** use **File → Check for updates…**. **If quewi
stays open after you click Yes, close it yourself.** The update then
installs. That's the bug this release fixes, so from 1.0.2 onwards quewi
closes itself.

### New

- **Effects presets.** A **Presets** menu on the audio editor's effects
  rack with 28 ready-made chains: Voice (Telephone, Old radio, Megaphone,
  Walkie-talkie, Voice clarity), Space (Next room, Cathedral, Canyon echo,
  Stadium announcer…), Character (Monster, Giant, Chipmunk, Robot, Ghost,
  Underwater…) and Mix. You can also save your own.
  [Effects rack →](../using-quewi/inspector.md#effects-rack-audio-cues-only)
- **Four new effects:** Distortion, Lo-Fi (bit crusher), Pitch Shift (higher
  or deeper without changing speed) and Tremolo (or auto-pan).
- **Import sound effects onto a pad.** Right-click an empty soundboard pad
  → **Import from URL…**. You get the same search, preview and download as
  the cue list importer, and the sound lands on that pad.
  [Import from URL →](../media-import.md)
- **Trim after importing.** The importer can open the download in the
  audio editor so you can cut one effect out of a long compilation.
- **Update Render.** Once a cue plays a rendered file, re-rendering updates
  that same file, with no save dialog. **Render As…** makes a new one.

### Fixed

- **In-app updates finish by themselves.** After you said Yes, quewi
  didn't close, so the installer sat waiting until you closed quewi
  yourself. quewi also asks about unsaved changes *before* the installer
  starts now, not after.
- **Effect changes in the audio editor are saved.** Changing only
  effects used to be lost when you closed the editor.
- **Effects no longer play twice after a render.** The render already
  contains them, so quewi no longer applies them live on top.
- **Shrinking the soundboard asks first** before removing pads that no
  longer fit (on every layer).
- **Fade All over OSC** (`/quewi/fadeAll`) now cancels pending
  auto-continues, like the button does.

---

## 1.0.1 (September 2026)

The first patch after 1.0. It adds soundboard keybinds and a set of mixing
features, and fixes a long list of bugs found in a front-to-back audit of
the app.

**Updating from 1.0.0:** use **File → Check for updates…**. If the in-app
update doesn't finish, download the installer from the
[releases page](https://github.com/ServeGaming/quewi/releases) and run it.
Your shows and settings are kept either way.

### New

- **Soundboard keybinds.** Give every pad its own key: right-click the pad
  → **Set keybind…**. On Windows, the keys can work **system-wide**, so
  pads fire even while quewi is in the background behind a game, Discord or
  OBS. [Soundboard →](../using-quewi/soundboard.md#keybinds)
- **DCA GO in the transport bar.** Fire the next DCA cue at the console
  from anywhere in quewi. [Transport →](../using-quewi/transport.md#dca-go)
- **Linked cues.** Pair a sound cue with a DCA cue, and one GO fires both,
  from either end. [quewi Mix →](../using-quewi/mix.md#linking-a-sound-cue-to-a-dca-cue)
- **DCA picker.** Double-click a DCA cell to tick who's on it, instead of
  typing names. [quewi Mix →](../using-quewi/mix.md#writing-cues)
- **Tear-off Inspector sections.** Drag a section's title out to float it,
  on another monitor if you like, and close it to dock it back.
  [Inspector →](../using-quewi/inspector.md#tearing-off-a-section)
- **Coffee theme** (View → Theme → Coffee).
- The soundboard's **+ Add layer** button now looks like an add button.

### Fixed: playback

- **Auto-continue waits for post-wait** before firing the next cue, and GO's
  pointer skips past the whole chain, so the next GO doesn't re-fire a
  running cue.
- **Pause is a real pause.** Sound, video and every pending wait freeze in
  place, and pressing Pause again (now labelled **Resume**) picks everything
  up. It used to stop the audio for good and black out the lights.
- **Fade All** fades lights instead of cutting them to black. Fade All and
  Panic (including over OSC) cancel pending pre-waits and auto-continues so
  nothing fires afterwards.
- Group cues, Start/Stop/Goto targets, and cues that trigger themselves are
  all fixed. A self-triggering chain can no longer hang the app.

### Fixed: audio

- **Fade cues hold their level.** They used to ramp down, then snap back to
  where they started.
- A **paused cue now stops** on Stop or Panic.
- **Loops** wrap cleanly to the trim-in point, with no gap, no replaying of
  trimmed audio, and no re-fade on every pass.
- A cue's **Fade Out** setting is now applied (it was stored but ignored),
  and fade-in is measured from trim-in.
- **Far less RAM for long files.** A 3-hour track used to hold about 4 GB of
  memory. It now uses about 45 MB, because long files are decoded to a
  temporary cache on disk and streamed from there. Every file also loads a
  little faster.

### Fixed: quewi Mix

- **A DCA GO only touches the mics in your show.** It used to mute and
  un-assign every channel on the desk, including band, playback returns and
  talkback.
- Clicking the Mix tab or editing a cell no longer resets the DCA playhead
  to cue 1.
- Renaming an ensemble or renumbering a mic carries through to every cue
  that uses it. Before, those mics were muted on GO.
- **X32:** quewi now notices when the console goes away and reconnects by
  itself. Also fixed a race on connect that could skip an assignment.
- **DM7:** full DCA rows are written until quewi knows the desk's state,
  so a fresh connection can't leave stale assignments behind.

### Fixed: everything else

- **Show Mode really locks.** Delete, undo, the new-cue keys and even
  Close Show still worked through their shortcuts mid-show. Now every
  editing action is off, and the mix page and detached lists lock too.
- **Crash recovery keeps your work.** A recovered show used to open marked
  as saved, with its recovery file already deleted, so closing it lost the
  work. It now opens as unsaved and stays protected until you save.
- **Esc in the audio editor** stops the editor's preview. It used to
  trigger a full Panic.
- **Soundboard Stop All** stops only the soundboard. It used to stop the
  cue list and the lights as well.
- **Keyboard shortcuts no longer collide.** Cue-creation keys only work in
  the cue list, so typing on the Soundboard or Mix page no longer creates
  cues. **New MSC cue moved to `Ctrl + Alt + M`** because it clashed with
  the Mix grid shortcut, and neither worked.
- Soundboard, mix and patch edits now mark the show as unsaved, so you're
  asked before closing without saving.
- Shrinking the soundboard now asks before removing pads that no longer fit.
- The audio editor now matches your theme.

---

## 1.0.0 (July 2026)

The first stable release: quewi Mix (DCA mixing for X32/M32 and DM7),
channels and ensembles, a full design pass across every theme, soundboard
layers, OSC control of the whole show, and a crash audit.
