# Licensing — design (2026-10-28)

Matthew is handing quewi to community theatres to try. Each gets a key that
works on **one computer** for **14 days from activation**; nothing in quewi is
usable until a key is in; updates never reset or shorten the 14 days; it must
keep working offline on show night. And it should resist "ask an AI to remove
the check".

## Decisions (Matthew, 2026-10-28)

- Source goes **private**; installers, updates and the docs site move to a
  public binaries-only repo (`ServeGaming/quewi-releases`). While the source is
  public, anyone can delete the check and build — no scheme survives that.
- Server: Supabase project **Quewi** (`mdvoeinmfjcyedvuffwq`, its own
  account/org). **Never** touch the Pawn Tiger Supabase account (the MCP
  connector in Claude sessions is logged into Pawn Tiger — don't use it for
  quewi; use the Supabase CLI, logged into the quewi account).
- 14 days start at **activation** (first time the key is entered on a machine).
- Internet needed **only to activate**; then the signed license works offline;
  quewi checks in when it can (revocation, clock check).

## How it works

```
 theatre's quewi                 Supabase (Quewi)                 Matthew
 ───────────────                 ────────────────                 ───────
 Activation screen ──key + machine hash──▶ fn activate ──▶ licenses / activations
        ◀───────── signed license (Ed25519) ────────┘         ▲
 verifies with the public key built into quewi              admin panel
 stores it (QSettings); 14 days counted from activation      (login, RLS)
 fn check (when online): revoked? server time
```

### The license (token)

`base64url(payload JSON) + "." + base64url(Ed25519 signature over the payload bytes)`

```json
{"v":1, "lic":"<license uuid>", "act":"<activation uuid>",
 "key":"QW-…-ABCD (last 4 only)", "name":"Theatre name",
 "mh":"<machine hash, hex>", "iat":1761600000, "exp":1762809600}
```

- Signed by the server with the private key (Supabase secret
  `LICENSE_SIGNING_KEY`, PKCS#8 PEM; never in the repo or chat). quewi embeds
  the 32-byte public key and verifies offline (Monocypher's Ed25519).
- `mh` = lowercase hex SHA-256 of `"quewi-machine-v1:" + QSysInfo::machineUniqueId()`
  (Windows MachineGuid, macOS IOPlatformUUID, Linux /etc/machine-id). Unchanged
  by reinstalls and updates.
- `exp` = activation time + the key's `duration_days` (default 14). Fixed at
  first activation: activating the same key again on the **same** machine
  (reinstall, settings wiped) returns the same `exp`; on **another** machine it's
  refused.

### Server (supabase/)

- Tables (RLS on; no anon access): `licenses` (key, label, contact, duration_days,
  max_machines = 1, notes, revoked, created_at), `activations` (license_id,
  machine_hash, machine_name, app_version, activated_at, expires_at,
  last_seen_at, revoked), `admin_emails` (who may use the panel).
- Admin access: RLS policies allow everything to an authenticated user whose
  (confirmed) JWT email is in `admin_emails`. Public sign-ups off.
- Edge functions (no JWT; they use the service role internally):
  - `POST /activate {key, machineHash, machineName, appVersion}` →
    200 `{token}` · 404 unknown key · 403 revoked/expired · 409 used on another
    computer. Keys are compared case-insensitively, dashes/spaces ignored.
  - `POST /check {token}` → `{status: "ok"|"revoked"|"expired"|"invalid", exp, serverTime}`
    and updates `last_seen_at`.

### quewi (src/app/License*)

- Built with `QUEWI_LICENSING=ON` only by the release workflow (CI tests and dev
  builds have it off, so tests and `--selftest` run without a key). Off → the
  gate never shows. Source is private, so an "off" build needs the source.
- Startup gate, before the main window exists (so no OSC, no shows, no
  editing): valid stored license → go; else the Activation screen (enter key,
  shows the machine name, "need a key? contact…"). Expired → says when, asks
  for a new key. Cancel quits.
- Clock: remembers the latest time it has seen (QSettings, also checked against
  the server time on each check); a clock wound back by more than a day counts
  as tampering → needs to go online once.
- Online check on start (async) and every 6 h: revoked → locks on next start
  (a running show is never killed mid-performance; a banner says so).
  "Trial ends in N days" shows from 3 days before.
- Defence in depth: the license is re-verified (signature + machine + time) at
  several independent places (start, opening/saving a show, GO engine start),
  not one boolean; release builds have no debug symbols.

### Honest limits

Anything that runs on the customer's computer can, with enough skill, be
patched. This stops copying, sharing keys, clock tricks and "remove the check
from the source" (no source), and makes binary patching a real job rather
than a prompt. It's not DRM against a determined cracker; the licence terms
are the backstop.

## Admin panel

`admin/index.html` — a single page (open the file, or host it anywhere):
log in (Supabase Auth, email + password), list / search keys with their
activation (machine, activated, expires, last seen), create keys (label,
contact, days, notes; key shown once with a copy button), revoke / un-revoke,
reset a key's machine (lets them move to a new computer, same expiry), extend
or shorten, delete. Uses only the public anon key; everything is enforced by
RLS.
