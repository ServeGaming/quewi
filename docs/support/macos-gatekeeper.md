# macOS Gatekeeper

If macOS told you "quewi is damaged and can't be opened", this is
the page for you. The app itself is fine — Apple's anti-malware
system is being overprotective of a small open-source project
that hasn't paid the $99/year Apple Developer Program fee.

There are three escape hatches, easiest first.

---

## Fix 1 — the bundled recovery script

When you mount the quewi DMG, you'll see four items:

```
quewi.app  →  Applications        ← the install drag target
README.txt    Fix Gatekeeper.command  ← the recovery
```

**Double-click `Fix Gatekeeper.command`.**

It opens Terminal, asks for your Mac password (so it can write to
`/Applications`), strips the quarantine flag from `quewi.app`,
and launches the app.

You only need to do this once per downloaded version.

---

## Fix 2 — right-click → Open

If you got the milder **"quewi cannot be opened because the
developer cannot be verified"** instead of "damaged":

1. Open `/Applications` in Finder.
2. **Right-click** (or Control-click) `quewi.app`.
3. Choose **Open**.
4. Confirm at the warning dialog.

After this once, double-clicking quewi works forever.

---

## Fix 3 — Terminal one-liner

If neither of the above works:

```sh
sudo xattr -cr /Applications/quewi.app
open /Applications/quewi.app
```

`xattr -cr` recursively strips every extended attribute (including
`com.apple.quarantine`, which is what Gatekeeper checks).
`sudo` is needed because writing to anything in `/Applications`
requires admin privileges.

---

## Last resort — System Settings whitelist

Sometimes macOS lets you whitelist a blocked app through the
Privacy & Security pane:

1. Try to launch quewi (it'll fail with the warning).
2. Open **System Settings → Privacy & Security**.
3. Scroll to the **Security** section near the bottom.
4. You'll see a line like *"quewi was blocked from use because it
   is not from an identified developer"* with an **Open Anyway**
   button. Click it.
5. Try launching quewi again — confirm the warning that appears.

This is the path Apple intends; the recovery script just shortcuts
it.

---

## Why is this happening?

Three things have to be true for Gatekeeper to show the
right-click escape:

1. The bundle has a code signature
2. The signature is from a registered Apple Developer ID
3. The bundle has been submitted to Apple's notary service and
   come back "approved"

Quewi currently has #1 (ad-hoc — every CI build self-signs) but
not #2 or #3. Apple Silicon strict mode treats `(#1 = ad-hoc)
+ (#2 = no real ID)` as "this is suspicious" and shows the
"damaged" prompt instead of the milder "unidentified developer"
one.

The right-click trick still works on Intel Macs. Apple Silicon
strict mode is the harder case — and the recovery script is the
designed-for-users escape hatch.

---

## When does this stop being a problem?

When quewi ships with:

- **A real Apple Developer ID** ($99/year), AND
- **Notarization** for each release (Apple's notary service scans
  every DMG and signs off on it)

Both are tracked in the project's [v1.0 roadmap](https://github.com/ServeGaming/quewi).
Once they ship, downloads will open with no prompt at all — same
behaviour as any other Mac app.

---

## "I don't trust running random Terminal commands from a docs page"

Fair. Here's what each command does:

- `sudo xattr -cr /Applications/quewi.app` — runs the system's
  built-in `xattr` (extended attribute) tool with the `-c`
  (clear) and `-r` (recursive) flags, on the quewi app bundle
  in your Applications folder. It removes file metadata,
  including the `com.apple.quarantine` flag macOS added when
  Safari/Chrome downloaded the DMG. It doesn't run any code
  from the quewi bundle, and it doesn't touch anything outside
  `/Applications/quewi.app/`.

- `open /Applications/quewi.app` — runs the system's `open`
  command on the now-unquarantined app, the same way a
  double-click would.

The `Fix Gatekeeper.command` script runs exactly these two
commands. You can inspect it before running — right-click →
**Open With → TextEdit**.

---

## Open the issue tracker

If none of these fix it for you,
[open an issue](https://github.com/ServeGaming/quewi/issues/new)
with:

- macOS version (Apple menu → About This Mac)
- Mac model (Intel vs Apple Silicon — visible on the same screen)
- Exact wording of the error dialog
- Output of `codesign --verify --verbose=2 /Applications/quewi.app`
  in Terminal
