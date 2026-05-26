# Troubleshooting

Things that go wrong, and what to do about them.

If your specific symptom isn't here, check the [FAQ](faq.md) and
then [open an issue](https://github.com/ServeGaming/quewi/issues)
with the version (Help → About), the OS, and exactly what you
did to trigger the problem.

---

## Install / launch

### Windows: "Windows protected your PC" / SmartScreen warning

Expected for unsigned downloads. Click **More info** →
**Run anyway**. One-time per version. A future release with an
EV code-signing certificate will skip this.

### macOS: "quewi is damaged and can't be opened"

Apple Silicon strict Gatekeeper. See the
[macOS Gatekeeper](macos-gatekeeper.md) page for the full story.
Quick fix:

```sh
sudo xattr -cr /Applications/quewi.app
open /Applications/quewi.app
```

### macOS: "quewi" can't be opened because the developer cannot be verified

Less alarming variant of the above. **Right-click**
(or Control-click) `quewi.app` → **Open** → confirm.

### Linux: AppImage won't launch

Common causes:

- **Missing `libfuse2`** — `sudo apt install libfuse2`
- **Missing exec bit** — `chmod +x quewi-X.Y.Z-linux-x86_64.AppImage`
- **Missing GUI libs on a headless distro** — install `libxkbcommon0`
  and `libfontconfig1`

Diagnostic: run the AppImage from a terminal — the error message
that prints is usually the actual fix.

---

## Audio

### Cue plays, no sound from speakers

Check, in order:

1. **System volume** isn't muted (yes, really).
2. **quewi's master device** — Preferences → Audio → Output device.
   Quewi defaults to the OS default output. If you have a USB
   interface that should be primary, set it explicitly.
3. **Per-cue gain** — Inspector → Audio cue's gain slider. If
   it's at `-INF`, the cue plays silently.
4. **Output matrix** — for cues with custom routing, make sure
   the FOH (or whichever zone you want) row isn't muted.
5. **Bit depth / sample rate mismatch** — Qt Multimedia handles
   resampling automatically but some USB DACs cough on weird
   combinations. Try a 44.1 / 16-bit master rate.

### Audio crackles or stutters

Buffer-under-run. The audio thread isn't getting CPU time fast
enough.

- **Close other audio apps.** Browsers, video calls, screen
  recorders all share the audio device on most OSes.
- **Increase the buffer size** — Preferences → Audio → Buffer
  size. Default is small for low latency; bumping it to 1024
  or 2048 samples trades latency for headroom.
- **Pre-warm cues** — quewi auto-prewarms but if your file is
  on a slow disk or network mount, the first GO is the one that
  pays the decode cost. Move show audio to a local SSD.

### "Audio still decoding" warning on GO

Decode hasn't caught up. Either the file is very long (full-show
multitrack masters) or the source is on a slow disk. Quewi will
still play once decode catches up — usually a fraction of a
second. If it persists, copy the audio to a local SSD.

---

## Video

### Video cue fires but the window is black

Most common: the **screen index** in the Inspector points at a
display that doesn't exist (e.g. you set it for a 3-monitor
rig but you're on a laptop right now). Set it to `0` for
preview.

Second most common: the **geometry** is set to a 0-size rectangle.
`posW` and `posH` are normalized (0..1) — if either is `0`, you
get no visible surface.

### Video plays without audio

By design — VideoCue plays the video; if you want the audio track
of that file too, add an Audio cue pointing at the same file and
fire them as a Group cue (so they fire simultaneously).

---

## Cue list

### GO doesn't fire the cue I expected

quewi fires the **next-up cue**, shown in the transport bar.
That's the cue currently selected, OR the row after the last cue
that fired. Click directly on the cue you want to fire and press
GO — that's the safe way.

### "No cue selected" — but I have cues

The selection clears when you click outside the cue list (e.g.
on the Inspector). Click any row in the list and the selection
restores.

### Drag-reorder doesn't work / context menu is missing

You're in [Show Mode](../using-quewi/show-mode.md). The lock
suppresses editing actions. Toggle Show Mode off
(<kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd>) to edit.

---

## OSC

### OSC cue fires but nothing happens on the receiver

Check, in order:

1. **Both machines on the same network** — for cross-machine OSC
   on UDP, both endpoints must be reachable. Ping the receiver's
   IP from quewi's machine; if that fails, you've got a network
   problem upstream.
2. **Firewall** — Windows Firewall and macOS Application Firewall
   both block UDP traffic by default for unsigned apps. Allow
   quewi through.
3. **Correct port** — typing `8000` when the receiver is on
   `9000` is silent failure. Double-check.
4. **Wireshark / OSC monitor** — the receiver app's OSC monitor
   (if it has one), or Wireshark filtering on `udp.port == 8000`,
   shows whether the packet actually left quewi.

### Remote-control OSC (incoming to quewi) doesn't work

Check:

1. **Port matches** — Preferences → OSC → UDP port. Default is
   `53535`.
2. **Listener is on** — the Pre-flight panel reports OSC listen
   status, or check the OSC Monitor (Tools menu).
3. **Address pattern** — `/quewi/go` not `/quewi/Go` (case-
   sensitive). See the [OSC reference](../osc-control/reference.md)
   for the exact addresses.

---

## Updates

### In-app updater says "no update available" but a new tag exists

The release is still building. CI takes ~10 minutes after a tag
push. Wait, then click **Check for updates** again.

### macOS update downloads but doesn't replace the app

Means the running `.app` isn't writable. Move quewi out of a
read-only location (e.g. you ran it directly off the mounted
DMG instead of dragging to Applications first). Drag to
`/Applications/` and try again.

### Windows update says "downloaded but couldn't run"

The MSI path tripped on SmartScreen / Defender / UAC. Open the
folder shown in the failure dialog and double-click the `.msi`
manually — same effect, just one extra click.

If you're running the portable ZIP, no MSI dialog should appear —
the swap happens in-place. If it isn't working, check
[in-app updates](updates.md) for the diagnostic flow.

---

## Performance

### CPU at 100% when idle

quewi shouldn't be doing anything if no cue is running. Check
**Help → About → Build info** to confirm you're on a release
build, not a debug build. Debug builds are 20× slower for some
operations.

Also: the OSC monitor window's table view repaints on every
incoming message. If you have a chatty external app sending
thousands of OSC messages per second and the monitor window
open, that's the culprit — close the monitor when you don't
need it.

### Long cue list (1000+ cues) is sluggish

quewi virtualizes the cue list — drawing only what's on screen —
but some operations (renumber-all, find-and-replace) are O(N).
Generally fine up to about 10,000 cues; past that, split into
separate cue lists.

---

## Crash recovery

### quewi crashed mid-show

It writes a journal of every edit. On next launch you'll see a
"Recovered journal" dialog offering to restore. Pick **Recover**.

If you don't see the dialog, the journal is at
`<user-config-dir>/journals/<workspace-name>.journal`. Open the
workspace, then File → Recover from journal.

---

## Asking for help

When opening an issue, include:

- **Version**: Help → About → version line
- **OS**: Windows 11 22H2 / macOS 14.5 / Ubuntu 22.04
- **What you did**: step-by-step. "I opened a show, added a cue,
  pressed GO" beats "it doesn't work."
- **What you expected** vs **what happened**
- **The debug log** if launch fails: written to
  `<user-data-dir>/quewi.log`. Attach the file (or paste the
  last 100 lines).

[Issue template →](https://github.com/ServeGaming/quewi/issues/new)
