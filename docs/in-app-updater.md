# In-app updater architecture

Goal: replace the "download installer, run it, click through prompts,
relaunch" loop with a one-click "Update now → app restarts on the new
version" experience. No installer dialogs, no admin prompts (when
possible), no manual file management.

Status as of v0.9.46:

| Platform | In-place update | Status |
|---|---|---|
| Linux (AppImage) | yes | **shipping** |
| macOS (.app drag-install) | yes — DMG mounted programmatically, .app rsync'd over running bundle | **shipping** |
| Windows (portable mode) | yes — needs CI to produce a portable ZIP | planned |
| Windows (MSI install) | no — falls back to running the new MSI | shipping |

---

## How the Linux path works

When quewi is running as an AppImage, the AppImage runtime sets the
environment variable `$APPIMAGE` to the absolute path of the running
file. The in-app updater takes advantage of this:

1. `UpdateInstaller` downloads the new `.AppImage` to `~/Downloads`
   (same as today).
2. `launchInstaller` reads `$APPIMAGE`. If non-empty AND the file
   exists, it writes a tiny shell helper to `/tmp/quewi-update-<pid>.sh`:

   ```bash
   #!/bin/bash
   set -e
   # Wait for quewi to exit (PID poll, max 30 s)
   for i in $(seq 1 60); do
     if ! kill -0 "$PID" 2>/dev/null; then break; fi
     sleep 0.5
   done
   # Swap in the new file
   mv -f /home/user/Downloads/quewi-X.Y.Z.AppImage /home/user/Apps/quewi.AppImage
   chmod +x /home/user/Apps/quewi.AppImage
   rm -f "$0"  # helper self-destructs
   exec /home/user/Apps/quewi.AppImage
   ```

3. quewi spawns the helper detached and quits 1.5 s later.
4. The helper waits, swaps, execs the new AppImage in place.

The user ends up running the new version at the same path they were
running before. No extra files in Downloads, no manual cleanup.

If `$APPIMAGE` is unset (someone's running a locally-built binary
directly, e.g. `./build/linux-release/src/app/quewi`), the updater
falls back to running the downloaded AppImage as a separate process
without trying to swap. Safer — we don't want to step on a dev
build.

---

## How the macOS path works

Drag-install Mac users have a `.app` bundle they wrote to. The
running quewi knows its own bundle path via
`QCoreApplication::applicationDirPath()` (which points to
`.../quewi.app/Contents/MacOS`).

1. `UpdateInstaller` downloads the `.dmg` to `~/Downloads`.
2. `launchInstaller` checks that the running `.app` is writable.
   If yes, writes a tiny shell helper to `/tmp/quewi-update-<pid>.sh`:

   ```bash
   #!/bin/bash
   set -e
   # Wait for quewi to exit
   for i in $(seq 1 60); do
     if ! kill -0 "$PID" 2>/dev/null; then break; fi
     sleep 0.5
   done
   # Mount the DMG headlessly
   MOUNT=$(hdiutil attach -nobrowse "$DMG" | grep '/Volumes/' | tail -1 | sed ...)
   # rsync over the running bundle (--delete drops removed files)
   rsync -a --delete "$MOUNT/quewi.app/" "$BUNDLE/"
   xattr -cr "$BUNDLE"                   # strip quarantine
   hdiutil detach "$MOUNT" -quiet
   rm -f "$DMG"
   open "$BUNDLE"
   rm -f "$0"
   ```

3. quewi spawns the helper detached and quits 1.5 s later.
4. The helper waits, mounts, rsyncs, detaches, relaunches.

If the running `.app` isn't writable (rare, but possible if quewi
was installed via a system admin), or if it's not actually inside a
`.app` bundle (running directly from a build dir), the updater
falls back to opening the `.dmg` in Finder the way it did before.

Once we have a real Apple Developer ID + notarization, the
`xattr -cr` step becomes unnecessary but is harmless.

---

## Windows plan (not yet implemented)

Windows is the hardest because you can't `mv` a running `.exe`. Two
paths depending on install mode:

### Portable install (zip extracted to a user-writable folder)

1. Add a portable-zip artifact to CI: `quewi-X.Y.Z-win64-portable.zip`
   containing `quewi.exe` + all Qt DLLs + every windeployqt'd
   dependency. Same files the MSI installs, just zipped.
2. In-app updater detects portable mode by checking if the install
   dir is writable AND there's no entry in HKLM's Uninstall registry
   for quewi.
3. Download the new portable zip, extract to a temp dir.
4. Write a tiny `quewi-updater.bat` next to quewi.exe:

   ```batch
   @echo off
   ping -n 3 127.0.0.1 > nul        :: ~2-second delay
   robocopy /e /move "%TEMP_DIR%" "%INSTALL_DIR%"
   start "" "%INSTALL_DIR%\quewi.exe"
   del "%~f0"                       :: helper self-destructs
   ```

5. Spawn `quewi-updater.bat` detached. Quit.

### MSI install (current default — Program Files write requires admin)

Stays as-is. The MSI bumps versions in-place; Windows Installer's
own Restart Manager closes quewi mid-install. No in-app shortcut
is possible without elevation.

**Work needed:**
- Add portable-zip artifact to `.github/workflows/release.yml`.
- Install-mode detection in `UpdateInstaller`.
- Helper batch generation and launch.
- The helper batch itself should be code-signed when we have a
  Windows code-signing cert (separate from Apple Developer ID).
  Until then SmartScreen will warn the first time the helper runs.

---

## Why not just use Sparkle / Squirrel / WinSparkle?

- **Sparkle (macOS)** — fantastic library; we'd lean on it if quewi
  were mac-first. As a cross-platform Qt app, integrating Sparkle
  means adding a Cocoa framework and writing the Objective-C glue.
  Tractable, just adds ~500 KB to the bundle and one more dependency.
- **WinSparkle** — port of Sparkle to Windows. Smaller surface area
  than rolling our own helper but still per-platform; doesn't save
  much vs. the batch-helper approach.
- **Squirrel.Windows** — designed for Electron-style apps; complex
  setup, not Qt-friendly.

For a small Qt app with three platforms, hand-rolling the helper is
~200 lines per platform and keeps the dependency surface flat. The
linux path is already done in <60 lines. Mac and Windows would each
land in a similar order of magnitude.

---

## What ships today (Linux)

After updating to v0.9.46 via the in-app prompt:
- The downloaded AppImage replaces the running one at its current path.
- quewi restarts to the new version automatically.
- Nothing is left in `~/Downloads` cluttering the user's folder.

Verified to work as long as the user's current AppImage is on a
writable filesystem (it normally is — Linux users typically run
AppImages from `~/Apps`, `~/Downloads`, or `~/.local/bin`).
