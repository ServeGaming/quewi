# FAQ

Common questions, short answers. For diagnostic flows see
[troubleshooting](troubleshooting.md).

---

## General

**Is quewi free?**
Yes. AGPL-3.0 licensed. You can't be charged for it; nobody can
distribute a paid fork either (the license forbids it). See the
[license](../about/license.md) page.

**Is the source code public?**
Yes — [github.com/ServeGaming/quewi](https://github.com/ServeGaming/quewi).
File issues, send PRs, fork freely.

**Does quewi phone home?**
Only for the optional in-app update check. It hits GitHub's
public API to list releases — no quewi-operated server is
involved at any point. Turn it off in Preferences → General →
**Check for updates on launch**.

**What's the production-readiness?**
Quewi is currently at v0.9.x. The feature surface is real and
extensive, but the project hasn't yet been load-tested in
production runs that span multiple shows. We're tracking the
1.0 release through real-world testing.

---

## Compatibility

**Will quewi read QLab files?**
Not natively. QLab uses its own bundle format. A best-effort
importer might land later; until then the migration path is
manual — match the cue list, copy file paths.

**Can I run quewi headless?**
Sort of. There's no `--headless` flag, but quewi supports
`--selftest` which boots the engines without showing the main
window for a few seconds and exits. Driving a real show
headlessly isn't supported.

**Does it work over remote desktop / VNC?**
Yes for editing. Show running over remote desktop is not a
good idea — audio routing gets weird (the audio plays on the
*remote* machine; that may not be what you want) and key
events can be intercepted.

**Multi-user editing?**
Not built in. One quewi instance, one user. For multi-operator
control, point a second quewi at the first over OSC — quewi can
remote-control quewi.

---

## Hardware / setup

**Minimum specs?**
- 2 GB RAM
- Any 64-bit CPU from the last 10 years
- An audio output device (built-in is fine for testing)

A laptop is plenty for shows up to maybe 30 simultaneous audio
voices + a couple of video outputs. Past that, a desktop with a
discrete GPU helps the Qt RHI compositor.

**Does it work with my audio interface?**
If your OS recognises the interface, quewi does too (it goes
through Qt Multimedia, which goes through CoreAudio / WASAPI /
ALSA). Select it in Preferences → Audio → Output device.

**Does it work with my DMX interface?**
- **sACN over Ethernet** — yes, multicast (default) or unicast
- **Art-Net over Ethernet** — yes
- **Enttec Open DMX (USB serial)** — yes
- **Enttec DMX USB Pro** — yes (uses the same serial protocol)
- Other proprietary USB-DMX interfaces — depends. If it presents
  as a serial COM port (USB-RS485), it'll likely work.

**Does it work with my MIDI interface?**
If your OS sees it (System Settings → MIDI Studio on macOS,
Device Manager on Windows, `aplaymidi -l` on Linux), quewi sees
it via RtMidi.

---

## OSC

**What OSC version does quewi support?**
OSC 1.1, including bundles, time tags, all standard type tags
(`i f s b h t d S c r m T F N I [ ]`), pattern matching, and
multiple transports (UDP, TCP/SLIP, WebSocket).

**Can I control quewi from Companion / Stream Deck / TouchOSC?**
Yes — any OSC client. Address surface is documented in
[OSC reference](../osc-control/reference.md). For Companion
specifically, a generic OSC module pointed at quewi's IP works
today; a dedicated module might land later.

**Can quewi talk to Eos / GrandMA / Hog4?**
Yes — those consoles all speak OSC. Quewi's OSC cue type
constructs arbitrary messages; configure host + port + address
+ args per the console's manual.

**Can quewi do MIDI Show Control?**
Yes — MSC cue type. Supports the standard command-format /
command / Q-number / Q-list / Q-path fields. Tested against
ETC Eos and Hog4 simulators; real-console parity audit is
v1.0+.

---

## Show running

**What's the smallest cue you can fire?**
A Memo cue with no content does nothing — it's purely for
operator notes ("scene shift here", "remember light pre-set").
GO walks past it like any other cue. Useful for organising long
shows.

**Can I pre-load cues so GO is instant?**
Audio and video cues pre-decode the first chunk of their files
on import + every show open. GO then triggers playback from a
buffer that's already warm — typically sub-5-ms.

**What's the safest way to stop a show that's gone wrong?**
Panic (<kbd>Esc</kbd>) — 50 ms audio fade-out + lighting
blackout + every video surface stopped. Safer than yanking
cables.

**Can I undo during a show?**
Show Mode blocks undo. If you really need to revert in the
middle, leave Show Mode (PIN if you set one), undo, re-enter
Show Mode. The transport (GO / Pause / Fade-All / Panic)
itself isn't undoable — you can't un-fire a cue.

---

## File format

**Can I version-control my show files?**
Yes — they're SQLite, which is a single file, which Git handles
fine. Diffs aren't human-readable but the file size is small.

**Can I edit a `.quewi` file outside quewi?**
Yes, with any SQLite browser
([DB Browser for SQLite](https://sqlitebrowser.org/) is free).
Don't do this on a file quewi has open.

**Backup recommendations?**
Save often. Quewi auto-writes a journal of every edit to a
sidecar file; on crash recovery it offers to replay. For
production shows, copy the `.quewi` to a USB stick before
running.

---

## Development

**Can I build quewi from source?**
Yes — Qt 6.7+, CMake 3.24+, a C++23 compiler. The full
build instructions are in the [GitHub README](https://github.com/ServeGaming/quewi#building-from-source).

**Can I write a plugin?**
Not yet. Quewi doesn't have a plugin API — the source is the
extensibility surface. Forking the repo and adding what you
need is the supported path.

**Can I script cues with Lua?**
Not yet — Lua scripting is on the post-1.0 roadmap. For now,
the OSC remote control surface is the script-friendly
extension point.

---

## Still stuck?

- [Troubleshooting](troubleshooting.md) — symptom-by-symptom
  diagnostic
- [GitHub issues](https://github.com/ServeGaming/quewi/issues) —
  open one if your problem isn't covered
