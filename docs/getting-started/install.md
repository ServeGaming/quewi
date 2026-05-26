# Install quewi

Quewi runs on Windows 10/11, macOS 12 (Monterey) or newer, and most
modern Linux distributions. Downloads live on the [GitHub Releases
page](https://github.com/ServeGaming/quewi/releases) — every tag
ships with one artifact per platform.

=== ":material-microsoft-windows: Windows"

    Two flavours. Pick one.

    **MSI installer (recommended for most users)**

    1. Download `quewi-X.Y.Z-win64.msi` from the
       [latest release](https://github.com/ServeGaming/quewi/releases/latest).
    2. Double-click. Windows Installer installs quewi to
       `C:\Program Files\quewi\`. You'll see a UAC elevation prompt
       — that's normal for any Program Files install.
    3. Launch from the Start menu.

    Updates run through the MSI flow — the in-app updater
    downloads the new MSI and Windows Installer's Restart Manager
    closes quewi and lays down the new files.

    **Portable ZIP (recommended for one-click updates)**

    1. Download `quewi-X.Y.Z-win64-portable.zip`.
    2. Extract anywhere writable — `%USERPROFILE%\Apps\quewi\`,
       Desktop, even a USB stick.
    3. Double-click `quewi.exe`. No installer dialog.

    Updates from the portable ZIP swap files in place silently —
    one click in the update dialog, app restarts on the new
    version. No UAC prompt, no installer UI.

    !!! info "SmartScreen on first run"
        Both flavours are currently unsigned. Windows will show
        a SmartScreen "unrecognised app" dialog the first time
        you launch a fresh download. Click **More info** →
        **Run anyway**. You only need to do this once per
        version. A future release with a Windows EV code-signing
        certificate will skip this.

=== ":material-apple: macOS"

    1. Download `quewi-X.Y.Z-macos.dmg` from the latest release.
    2. Double-click to mount. A Finder window opens showing
       `quewi.app`, a shortcut to your `Applications` folder, a
       `README.txt`, and a `Fix Gatekeeper.command`.
    3. Drag `quewi.app` onto the `Applications` shortcut.

    **First launch:**

    1. Open your `Applications` folder.
    2. **Right-click** (or Control-click) `quewi.app` → **Open**.
    3. macOS will warn that the developer is unidentified —
       click **Open** to confirm. One-time confirmation.

    !!! danger "If you see 'quewi is damaged and can't be opened'"
        Apple Silicon's strict Gatekeeper mode sometimes shows
        this instead of the right-click escape prompt. Two
        fixes, easiest first:

        1. **Inside the DMG**, double-click
           `Fix Gatekeeper.command`. It strips the quarantine
           flag and launches quewi.
        2. **Or from Terminal:**
           ```sh
           sudo xattr -cr /Applications/quewi.app
           open /Applications/quewi.app
           ```

        See [macOS Gatekeeper](../support/macos-gatekeeper.md) for
        the full explanation.

    **Updates** — quewi mounts the new DMG headlessly, `rsync`s
    the new bundle over the running one, strips quarantine,
    relaunches. No "drag to Applications" again.

=== ":material-linux: Linux"

    1. Download `quewi-X.Y.Z-linux-x86_64.AppImage` from the
       latest release.
    2. Make it executable:
       ```sh
       chmod +x quewi-X.Y.Z-linux-x86_64.AppImage
       ```
    3. Run it:
       ```sh
       ./quewi-X.Y.Z-linux-x86_64.AppImage
       ```

    !!! tip "Put it somewhere permanent"
        AppImages run from anywhere — `~/Apps/quewi.AppImage`,
        `~/.local/bin/`, the Desktop. Wherever you put it,
        quewi's in-app updater will replace the file in place
        on update.

    **Required system libraries:**

    - libfuse2 (for AppImage runtime)
    - libxkbcommon, libfontconfig (for Qt's GUI bits)

    On Ubuntu/Debian:
    ```sh
    sudo apt install libfuse2 libxkbcommon0 libfontconfig1
    ```

    !!! note "Desktop integration"
        AppImages don't auto-register in your application menu.
        For a Start-menu-like entry, install
        [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher)
        — it offers to integrate on first run.

---

## Verifying the download

Each release artifact ships with a SHA256 hash on the GitHub
release page. To verify:

=== "Windows (PowerShell)"

    ```powershell
    Get-FileHash quewi-X.Y.Z-win64.msi -Algorithm SHA256
    ```

=== "macOS / Linux"

    ```sh
    shasum -a 256 quewi-X.Y.Z-macos.dmg
    sha256sum quewi-X.Y.Z-linux-x86_64.AppImage
    ```

Compare the hash to the one published on the release page. They
should match exactly.

---

## What gets installed

Quewi keeps its bits in three places:

| Path | Contents |
|---|---|
| Install location | `quewi.exe` / `quewi.app` / `quewi.AppImage` + Qt and FFmpeg libraries |
| User settings | OS-standard config dir: `%APPDATA%\ServeGaming\quewi\`, `~/Library/Preferences/com.ServeGaming.quewi.plist`, `~/.config/ServeGaming/quewi/` |
| Recent files + journals | Same as settings, under the `journals/` subfolder |

Your `.quewi` show files live wherever you save them — quewi never
moves them.

---

## Uninstalling

=== ":material-microsoft-windows: Windows"

    - **MSI install:** Settings → Apps → quewi → Uninstall
    - **Portable:** delete the folder

=== ":material-apple: macOS"

    - Drag `/Applications/quewi.app` to the Trash
    - Optionally remove preferences:
      `~/Library/Preferences/com.ServeGaming.quewi.plist`
      and `~/Library/Application Support/ServeGaming/quewi/`

=== ":material-linux: Linux"

    - Delete the `.AppImage` file
    - Optionally remove `~/.config/ServeGaming/quewi/`

---

## Next steps

- [Build a five-cue show in five minutes](quickstart.md)
- [Skim the concepts page](concepts.md) before you build a real show
