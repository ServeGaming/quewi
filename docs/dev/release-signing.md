# Code signing — Windows Authenticode + macOS notarization

quewi's installers are currently **unsigned**. For 1.0 they should be signed:
an unsigned app trips Windows SmartScreen ("Windows protected your PC") and
macOS Gatekeeper ("can't be opened because Apple cannot check it for malicious
software"), which most users read as "broken" — and it's tangled up with the
in-app updater, since the swapped-in binary must also be trusted.

This doc has everything needed to turn signing on. It's **not wired into
`release.yml` yet** because it needs paid certificates that only the project
owner can obtain; the release pipeline is load-bearing (the updater depends on
it) and isn't changed until the certs exist. When you have them, paste the
snippets below and add the listed secrets.

---

## Windows — Authenticode

### What you need
- A code-signing certificate. Options, cheapest → most trusted:
  - **Standard OV cert** (~$100-200/yr, e.g. Sectigo/DigiCert): signs fine but
    SmartScreen reputation builds up over time/downloads.
  - **EV cert** (hardware-token or cloud HSM): instant SmartScreen trust, but
    more expensive and the token complicates CI (cloud HSM signing is the CI-
    friendly path — Azure Trusted Signing / DigiCert KeyLocker).
- For a file-based OV cert, export it as a password-protected `.pfx`.

### Secrets to add (Settings → Secrets and variables → Actions)
| Secret | Value |
|---|---|
| `WINDOWS_CERT_PFX_BASE64` | `base64 -w0 cert.pfx` output (the whole .pfx, base64) |
| `WINDOWS_CERT_PASSWORD` | the .pfx export password |

### `release.yml` — add to the `build-msi` job, AFTER `cpack -G WIX`, BEFORE upload
```yaml
      - name: Sign MSI (Authenticode)
        if: env.WINDOWS_CERT_PFX_BASE64 != ''
        env:
          WINDOWS_CERT_PFX_BASE64: ${{ secrets.WINDOWS_CERT_PFX_BASE64 }}
          WINDOWS_CERT_PASSWORD:   ${{ secrets.WINDOWS_CERT_PASSWORD }}
        shell: pwsh
        run: |
          $pfx = "$env:RUNNER_TEMP\cert.pfx"
          [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERT_PFX_BASE64))
          $msi = Get-ChildItem build/windows-release -Filter *.msi -Recurse | Select-Object -First 1
          & "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" sign `
            /f $pfx /p $env:WINDOWS_CERT_PASSWORD `
            /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
            $msi.FullName
          Remove-Item $pfx
```
The `if:` guard means the step is **skipped** (release still succeeds, unsigned)
until the secret is set, so adding this changes nothing until you opt in.

---

## macOS — sign + notarize + staple

### What you need
- An **Apple Developer Program** membership ($99/yr).
- A **Developer ID Application** certificate (created in the Apple Developer
  portal → Certificates). Export it from Keychain as a password-protected
  `.p12` (include the private key).
- An **app-specific password** for your Apple ID (appleid.apple.com → Sign-In
  & Security → App-Specific Passwords) for `notarytool`.
- Your **Team ID** (Apple Developer → Membership).

### Secrets to add
| Secret | Value |
|---|---|
| `MACOS_CERT_P12_BASE64` | `base64 -i DeveloperIDApp.p12` |
| `MACOS_CERT_PASSWORD` | the .p12 export password |
| `MACOS_SIGN_IDENTITY` | e.g. `Developer ID Application: Your Name (TEAMID)` |
| `APPLE_ID` | your Apple ID email |
| `APPLE_TEAM_ID` | the 10-char Team ID |
| `APPLE_APP_PASSWORD` | the app-specific password |

### `release.yml` — add to the `build-dmg` job, AFTER macdeployqt, BEFORE `cpack -G DragNDrop`
```yaml
      - name: Import signing certificate
        if: env.MACOS_CERT_P12_BASE64 != ''
        env:
          MACOS_CERT_P12_BASE64: ${{ secrets.MACOS_CERT_P12_BASE64 }}
          MACOS_CERT_PASSWORD:   ${{ secrets.MACOS_CERT_PASSWORD }}
        run: |
          KEYCHAIN=$RUNNER_TEMP/build.keychain
          security create-keychain -p actions "$KEYCHAIN"
          security default-keychain -s "$KEYCHAIN"
          security unlock-keychain -p actions "$KEYCHAIN"
          echo "$MACOS_CERT_P12_BASE64" | base64 -d > "$RUNNER_TEMP/cert.p12"
          security import "$RUNNER_TEMP/cert.p12" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" \
            -T /usr/bin/codesign
          security set-key-partition-list -S apple-tool:,apple: -s -k actions "$KEYCHAIN"

      - name: Codesign the .app (deep, hardened runtime)
        if: env.MACOS_SIGN_IDENTITY != ''
        env:
          MACOS_SIGN_IDENTITY: ${{ secrets.MACOS_SIGN_IDENTITY }}
        run: |
          APP=$(find build/macos-release -maxdepth 3 -name 'quewi.app' -type d | head -1)
          codesign --force --deep --options runtime --timestamp \
            --sign "$MACOS_SIGN_IDENTITY" "$APP"
          codesign --verify --deep --strict --verbose=2 "$APP"

      # ... then `cpack -G DragNDrop` builds the .dmg from the now-signed .app ...

      - name: Notarize + staple the DMG
        if: env.APPLE_ID != ''
        env:
          APPLE_ID:          ${{ secrets.APPLE_ID }}
          APPLE_TEAM_ID:     ${{ secrets.APPLE_TEAM_ID }}
          APPLE_APP_PASSWORD: ${{ secrets.APPLE_APP_PASSWORD }}
        run: |
          DMG=$(find build/macos-release -maxdepth 2 -name '*.dmg' | head -1)
          xcrun notarytool submit "$DMG" --apple-id "$APPLE_ID" \
            --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
          xcrun stapler staple "$DMG"
```

Notes
- **Hardened runtime** (`--options runtime`) is required for notarization. quewi
  uses no JIT / unsigned-memory tricks, so the default entitlements are fine; if
  notarization later complains, add an `entitlements.plist` with
  `com.apple.security.cs.disable-library-validation` only if you load 3rd-party
  plugins (we don't today).
- The `.app` is signed **before** the DMG is built so the DMG contains the
  signed bundle; the DMG itself is then notarized + stapled.
- After this lands, the **updater's atomic swap** drops in an already-signed,
  notarized `.app`, so Gatekeeper stays happy across updates (no re-prompt).

---

## Verifying

- Windows: right-click the `.msi` → Properties → **Digital Signatures** tab.
- macOS: `spctl -a -vvv -t install quewi-<ver>-macos.dmg` should say
  *"accepted ... source=Notarized Developer ID"*.
