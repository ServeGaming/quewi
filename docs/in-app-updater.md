# In-app updater architecture

Goal: replace the "download installer, run it, click through prompts,
relaunch" loop with a one-click "Update now → app restarts on the new
version" experience. No installer dialogs, no admin prompts (when
possible), no manual file management.

Status as of v0.9.47:

| Platform | In-place update | Status |
|---|---|---|
| Linux (AppImage) | yes | **shipping** |
| macOS (.app drag-install) | yes — DMG mounted programmatically, .app rsync'd over running bundle | **shipping** |
| Windows (portable ZIP) | yes — ZIP extracted, batch helper swaps files in place | **shipping** |
| Windows (MSI install in Program Files) | no — install dir isn't writable; falls back to running the new MSI | shipping |

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

## How the Windows path works

Windows is the hardest because you can't `mv` a running `.exe`. The
solution is two artifacts + an install-mode branch.

### CI ships both an MSI and a portable ZIP

- `quewi-X.Y.Z-win64.msi` — for system-wide install in Program
  Files. Same as it's always been.
- `quewi-X.Y.Z-win64-portable.zip` — same set of files (quewi.exe
  + every windeployqt'd DLL) packaged as a flat ZIP. Drop the
  folder anywhere writable (`%USERPROFILE%\Apps`, Desktop, USB
  stick) and run `quewi.exe`.

### `UpdateChecker` picks the right artifact

Probes the running install dir for write access. If writable, it
prefers the `.zip` asset; if not (typical MSI install), it falls
back to `.msi`.

### `UpdateInstaller` swaps in place for the ZIP path

1. Download the ZIP via the existing flow.
2. Extract to `%TEMP%\quewi-update-<pid>\quewi\` using PowerShell's
   `Expand-Archive`.
3. Write a tiny `swap.bat` next to the staged folder:

   ```batch
   @echo off
   :wait
   tasklist /FI "PID eq %PID%" 2>nul | find "%PID%" >nul
   if not errorlevel 1 (
     ping -n 1 127.0.0.1 >nul
     goto wait
   )
   robocopy "%STAGE%" "%INSTALL%" /E /IS /R:2 /W:1 /NFL /NDL /NJH /NJS >nul
   start "" "%INSTALL%\quewi.exe"
   rmdir /S /Q "%STAGE%" 2>nul
   del "%DOWNLOAD%" 2>nul
   ```

4. Spawn `cmd.exe /c start "quewi-update" /min swap.bat`
   detached. Quit.

The batch polls for our PID to disappear, robocopies the new tree
over the install dir (`/IS` overrides identical-timestamp skips,
`/E` recurses subdirs — Qt's `plugins/` etc.), starts the new
`quewi.exe`, and cleans up after itself.

### Fallback (MSI install in Program Files)

If the install dir isn't writable, both the asset picker AND the
swap path skip. The updater downloads the MSI and runs it the way
it always has — Windows Installer's Restart Manager closes quewi
mid-install, lays down the new files, and the user relaunches.

### Code signing

The batch helper itself doesn't need to be code-signed — SmartScreen
flags binaries, not scripts running through `cmd.exe`. The downloaded
`quewi.exe` inside the ZIP would benefit from an EV cert eventually,
but that's a separate purchase from the Apple Developer ID.

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
