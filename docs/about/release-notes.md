# Release notes

## 1.0.4 (unreleased)

Songs that cue the lights, video cues that play their sound, and a
soundboard you can talk through.

### New

- **Lighting triggers.** Mark points and ranges on a song and quewi cues
  the lighting desk as the playhead passes them. Set your desk once
  (**Tools → Lighting Desk…**: ETC Eos / Ion / Element / Nomad, grandMA3, or
  grandMA2 over MSC), then pick what each trigger does from a list: GO, GO
  on another cue list, Stop, Back, go to a cue, set or bump a sub or fader,
  fire a macro, type a command line, or fire another cue in the show. No
  OSC knowledge needed. **Custom OSC / MIDI** is there for anything else:
  any OSC message, MIDI note, control change or program change, or MSC
  command. Ranges do one thing on the way in and another on the way out.
  Make them in the audio editor's new **Lighting** tab or straight on the
  timeline's lighting lane (click for a point, drag for a range). **Test**
  sends one now, and **Send while previewing** lets you program the desk
  against the editor's playback. **Tools → Lighting Triggers Armed** is the
  master switch.
  [Lighting triggers →](../using-quewi/lighting-triggers.md)
- **Beat grid.** Give a song its tempo (type it, tap it, or let quewi
  detect it) and the timeline shows the beats, triggers snap to them, and
  **Fill with beats…** puts a trigger on every beat or bar of a section,
  for example bumping a sub in time with the chorus.
  [Beat grid →](../using-quewi/lighting-triggers.md#beat-grid)
- **Video editor.** Double-click a video cue to open it in an editor laid
  out like Premiere Pro or DaVinci Resolve: a viewer with transport
  controls, an Inspector with the clip, video, audio and lighting
  settings, and a timeline with a picture track (V1) over the soundtrack
  (A1). Set In and Out points (drag them, or press **I** / **O** at the
  playhead) and drag fade handles for the picture and the sound. Space
  plays, the arrow keys step a frame, and every change is undoable. A
  trimmed video now starts and stops its picture at the trim too, not just
  its sound.
  [Video editor →](../cue-types/video.md#the-video-editor-new-in-104)
- **Splicing.** Cut sections out of the middle of a video: drag across
  the timeline and press **Delete**, or split it with the **Razor** tool
  (**B**) and delete a piece. **Keep only this** trims to a selection, and
  right-clicking a cut restores it. Picture, sound and lighting triggers
  all skip the cut, and the sound joins without a click. The file itself
  is never changed.
  [Splicing →](../cue-types/video.md#splicing-cutting-sections-out)
- **Video cues play their sound.** A video cue's soundtrack now goes
  through quewi's audio engine, with level, pan, fades, output and the
  full audio editor (**Edit sound…**). New video cues play their sound;
  shows saved before 1.0.4 open with it off, so they play exactly as
  before until you turn it on.
  [Video cues →](../cue-types/video.md#the-videos-sound-new-in-104)
- **Convert video ↔ audio cues.** **Convert to audio cue** keeps just the
  sound; **Convert back to video cue** restores the screen, position, size
  and opacity. The cue keeps its number and identity, so Fade, Start and
  Stop cues still find it, and sound edits and lighting triggers carry
  over. Also under **Cue → Convert Video ↔ Audio**. Undoable.
- **Send soundboard sounds to your mic.** Pads can play into a virtual
  cable (such as VB-Audio CABLE) that Discord, OBS or a game uses as its
  microphone, with your real voice mixed in.
  [Soundboard →](../using-quewi/soundboard.md#sending-sounds-to-your-mic)
- **Fit the Inspector.** Double-click the divider between the cue list and
  the Inspector to size the Inspector to its content.
  [Inspector →](../using-quewi/inspector.md#layout)
- **OSC remote API v5** (for HeliOSC and other remotes): lighting triggers
  (list, add, edit, remove, test, arm, and a `trigger/fired`
  notification), the lighting desk setting, beat grids, cuts, video ↔
  audio conversion, and the soundboard → mic settings.
  [Address reference →](../osc-control/reference.md#lighting-triggers-v5)

### Fixed

- **Space in the editors** plays and pauses wherever the focus is. Before,
  it pressed whichever button you'd last clicked.
- **Theme colours could go wrong at random.** A colour whose name started
  with another's (such as the row hover colour) could get the wrong value
  on some launches, so hover and selection colours were sometimes off.

- **OSC notifications never arrived for default subscribers.** Subscribing
  with the default pattern `/quewi/notify/*` matched none of the
  notification addresses, so remotes that relied on it received nothing.
  A pattern ending in `/*` now covers every level below it.
  [Subscriptions →](../osc-control/reference.md#subscribe-unsubscribe)
- **Setting a cue's file over OSC.** The file now starts decoding straight
  away, so the next GO plays it. Before, the first GO after a remote
  `set/filePath` found it still decoding and played nothing.
- **OSC `level`, `pan`, `seek` and `fx` on video cues** did nothing. They
  now drive the video's soundtrack, and `seek` moves the picture too.
- **OSC `/quewi/workspace/open <path>` lost unsaved changes.** It loaded
  straight over the current show; it now asks Save / Discard / Cancel on the
  quewi machine first, like New and Open….
- **Audio editor crash after removing a track.** Removing the active track
  while stopped left the editor and its effects rack pointing at the
  deleted track, so the next Play could crash. The editor now falls back
  to track 1 and closes any open EQ or compressor window for that track.
- **A second copy of quewi could delete the first one's recovery file.**
  Opening quewi while another copy had unsaved changes offered to "recover"
  that copy's live autosave, and answering No deleted it, leaving the
  running show without crash protection. Each copy now locks its own
  autosave, and recovery only offers ones whose quewi has actually closed
  or crashed.
- The video cue Inspector no longer shows a stray "Text size" label.

---

## 1.0.3 (September 2026)

A fix release for in-app updates.

!!! warning "Install this one by hand"
    The bugs fixed here are in the *old* version's updater, which is what
    runs an update. So from 1.0.2 or earlier, don't use Check for updates
    this time. Download `quewi-1.0.3-win64.msi` from the
    [releases page](https://github.com/ServeGaming/quewi/releases/latest)
    and run it (quewi closed). From 1.0.3 on, in-app updates work.

### Fixed

- **"Install update" just closed quewi.** The updater lost track of the file
  it had downloaded: by the time you clicked Yes, the path it held was
  garbage, so quewi crashed or said the file didn't exist. This was also
  behind the occasional "corrupted-looking path" in the update error.
- **Portable (zip) installs never updated in place.** The step that swaps
  the new files in was being started with a malformed command and silently
  never ran. It now runs, logs each step to `update-helper.log`, and always
  reopens quewi, even if the copy fails.
- The update prompt no longer promises a Windows permission prompt for
  portable installs, which don't need one.

The portable path was run end to end against the real 1.0.2 release:
download, quewi closing itself, files swapped, new version reopening,
clean-up. The installer (MSI) path shares the fixed download-and-close steps;
its install step already worked in the 1.0.0 → 1.0.1 update, once quewi was
closed.

---

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
