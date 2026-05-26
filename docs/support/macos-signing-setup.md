# macOS code signing + notarization setup

One-time setup so every release tag produces a signed, notarized
`.dmg` that opens with no Gatekeeper warnings on any Mac.

When the secrets below are present the release workflow signs with
your Developer ID, submits to Apple's notary service, waits for the
verdict, and staples the ticket. When the secrets aren't present
the workflow falls back to ad-hoc signing — useful for PR builds
and forks — so the path stays graceful.

---

## What you need from Apple

1. **A paid Apple Developer Program membership.** $99/year.
2. **A "Developer ID Application" certificate.** Create it inside
   Xcode (`Settings → Accounts → Manage Certificates → +`) or on
   developer.apple.com under Certificates → +. Pick "Developer ID
   Application" (not "Mac App Distribution" — that's for the App
   Store).
3. **Your Team ID.** A 10-character code shown at
   developer.apple.com → Membership Details.
4. **An app-specific password.** Generate one at appleid.apple.com →
   Sign-In and Security → App-Specific Passwords. Label it
   something like `quewi-ci`. You'll never see this string again
   after the dialog closes — copy it immediately.

---

## Export the certificate as a .p12

After the certificate is installed in your Mac's Keychain:

1. Open **Keychain Access**.
2. In the left sidebar pick **login** → **My Certificates**.
3. Right-click your `Developer ID Application: …` certificate →
   **Export**.
4. Format: **Personal Information Exchange (.p12)**.
5. Pick a password. Remember it — you'll paste it into a secret
   in a minute.
6. Save to a temp location, e.g. `~/Desktop/quewi-cert.p12`.

Now base64-encode it so it can go into a GitHub secret:

```sh
base64 -i ~/Desktop/quewi-cert.p12 -o ~/Desktop/quewi-cert.b64
```

Open `quewi-cert.b64` in any text editor and copy the entire
content (one long line of letters/numbers).

---

## Add the secrets to GitHub

Go to **github.com/ServeGaming/quewi → Settings → Secrets and
variables → Actions → New repository secret** and add these five:

| Secret name | Value |
|---|---|
| `MACOS_CERT_P12_BASE64` | The base64 blob from `quewi-cert.b64` |
| `MACOS_CERT_PASSWORD` | The password you chose during export |
| `APPLE_ID` | Your Apple ID email |
| `APPLE_TEAM_ID` | Your 10-character Team ID |
| `APPLE_APP_SPECIFIC_PASSWORD` | The `quewi-ci` app-specific password |

After saving the last one, **delete the local `.p12` and `.b64`
files** — they're no longer needed and shouldn't sit on disk.

---

## Verify it worked

Push a new tag. Open the workflow run on the Actions tab and look
for these steps under the `build-dmg` job:

- **Set up signing keychain** → "Using identity: Developer ID
  Application: Your Name (TEAM_ID)"
- **Codesign .app bundle (Developer ID + hardened runtime)** →
  "Verifying signature..." followed by a clean exit.
- **Sign DMG (Developer ID)** → ditto.
- **Notarize DMG** → "Submitting … to notarytool — this takes
  ~2-10 minutes..." followed by `status: Accepted`.
- **Staple notarization ticket to DMG** → "validates" output.

If any of those fail you'll see the actual reason in the log; the
most common cause is a missing entitlement (the bundle uses an
API the entitlements.plist doesn't grant — add it, push a new
tag).

---

## What the user sees after signing

Before signing: "quewi is damaged and can't be opened" on Apple
Silicon, "unidentified developer" on Intel.

After signing + notarization: **the app just opens.** Double-click
the DMG, drag to Applications, double-click quewi — no prompt at
all.

---

## Rotating the certificate

The Developer ID Application certificate is valid for ~5 years.
When it expires:

1. Generate a new one in Xcode → export as `.p12` as above.
2. Re-base64 it.
3. Update the `MACOS_CERT_P12_BASE64` and `MACOS_CERT_PASSWORD`
   secrets in GitHub.

The Team ID and Apple ID stay the same. The app-specific password
also stays the same unless you revoke it from appleid.apple.com.

---

## What if I want to sign locally (not just CI)?

Once the certificate is in your Keychain, a local sign + notarize
runs as:

```sh
codesign --force --deep --options runtime \
  --entitlements resources/macos/entitlements.plist \
  --timestamp \
  --sign "Developer ID Application: Your Name (TEAMID)" \
  build/macos-release/_install/quewi.app

# Then build the DMG via cpack, then:
xcrun notarytool submit build/macos-release/quewi-VERSION-macos.dmg \
  --apple-id "you@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password" \
  --wait

xcrun stapler staple build/macos-release/quewi-VERSION-macos.dmg
```

But the CI flow is the supported path — every release tag goes
through it. Local signing is only useful for one-off testing.
