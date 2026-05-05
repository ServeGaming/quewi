# Security policy & audit

This document records the security audit performed before the 0.1.0
release, the issues found and fixed, and the residual risks operators
should know about. Re-run the audit before each tagged release.

## Threat model

quewi is a desktop theatre cueing application. It is **not** a server,
not multi-tenant, and assumes the operator runs it on a machine they
control. The realistic adversaries are:

- A peer on the same LAN sending malformed OSC, MIDI, or HTTP.
- A malicious `.quewi` show file received from an untrusted source.
- A malicious media file referenced by such a show.

Out of scope: nation-state attackers, side-channel attacks on the
machine itself, threats requiring local code execution by the user.

## Findings (audited 2026-05-04)

### HIGH — Fixed

- **OSC codec integer overflow.** Hand-written decoder bounds-checked
  `cur + len > buf.size()` with 32-bit math; a near-INT_MAX `len` in a
  blob or bundle could overflow and bypass the check, leading to
  out-of-bounds reads.
  Fix: 64-bit math in every cursor-advance bounds check, plus a 256 MB
  hard cap on the input packet length up front.
  *Files:* `src/osc/OscCodec.cpp`.

- **OSC bundle/array recursion has no depth limit.** A peer could nest
  `#bundle` inside `#bundle` indefinitely and blow our stack.
  Fix: depth limit of 16 on `decodeBundle` / `decodeElement` /
  array-tag parsing.
  *Files:* `src/osc/OscCodec.cpp`.

- **SLIP decoder buffer was unbounded.** A TCP/SLIP peer could send a
  start-of-frame followed by infinite escaped bytes; we would grow our
  per-peer buffer until OOM.
  Fix: 1 MiB per-frame cap in `SlipDecoder::feed`. Frames exceeding
  the cap are silently dropped and the decoder resyncs at the next
  END byte.
  *Files:* `src/osc/OscSlip.{h,cpp}`.

- **OSC Query HTTP server had no request-size or per-connection
  timeout limits.** A client could send a multi-gigabyte request body,
  or hold a connection idle indefinitely (slow-loris).
  Fix: 16 KiB per-request cap with a 413 response, 2 s connection
  deadline that aborts the socket if the request hasn't completed.
  *Files:* `src/osc/OscQueryServer.cpp`.

### MEDIUM — Fixed

- **CueListView MIME drop count was unbounded.** A drop with our
  custom MIME type from a malicious sibling app would `reserve(N)` the
  list with no upper bound on N.
  Fix: 100 000-row cap, plus a check on the `QDataStream::status()`
  after each read so a malformed payload bails cleanly.
  *Files:* `src/ui/CueListView.cpp`.

### MEDIUM — Accepted, documented

- **Network listeners bind to all IPv4 interfaces.** `OscEngine`'s
  UDP, TCP/SLIP, and WebSocket listeners and `OscQueryServer` use
  `QHostAddress::Any[IPv4]`. This is what an operator on a stage
  network expects (control surfaces typically need to reach the show
  computer), but it means anyone reachable on the network can probe
  the OSC namespace.

  **Mitigation for operators:**
  1. Run quewi on a private show network, not a public/coffee-shop
     LAN.
  2. Bind the show machine's firewall to allow only the control-surface
     subnet.
  3. Don't open OSC listeners you don't need — leave the port off in
     Preferences if you only send OSC, never receive it.

  *Files:* `src/osc/OscEngine.cpp`, `src/osc/OscQueryServer.cpp`.

- **Show file payloads can reference arbitrary local paths.** Opening
  a `.quewi` from an untrusted source can cause the media stack to
  read files anywhere the user has access to (including UNC paths and
  network shares — DNS leak / SMB credential leak risk).

  **Mitigation:** treat `.quewi` files like macros in office documents
  — only open files from people you trust. A future release may add
  an "audit references" preflight step that lists every external path
  before any decode happens.

- **OSC and OSC Query have no authentication.** The OSC 1.0/1.1 specs
  don't define one. Anyone who can reach the listener port can fire
  any subscribed pattern. This is intentional — it's how the protocol
  works — but operators should keep listeners off untrusted networks.

### LOW — Accepted

- **OSC Query CORS allows any origin** (`Access-Control-Allow-Origin: *`).
  This is the normal posture for the OSC Query Protocol, and the
  server only serves namespace metadata; there is no privileged
  endpoint to exfiltrate. If an operator considers their cue-list
  topology sensitive, they should disable the OSC Query listener.

- **No TLS on any listener.** OSC + WS run plaintext. The protocol
  doesn't mandate TLS; in a theatre context the network is private.

### INFO — No issues found

- **SQL queries are uniformly parameterised.** Every `QSqlQuery` in
  `ShowFile.cpp` either uses `prepare()` + `addBindValue()` or
  hard-coded SQL. No string concatenation builds queries from
  user-controlled data.
- **No shell-out / process spawning.** The codebase contains no
  `QProcess`, `system()`, or `ShellExecute` calls.
- **No unsafe C string functions.** No `strcpy`, `sprintf`, `gets`.
  The only `memcpy` calls move endian-converted scalars or
  bounds-checked PCM.
- **No hardcoded credentials or tokens** in the source tree.

## Reporting a vulnerability

Open a private GitHub Security Advisory on the repository, or email
the maintainer listed in the GitHub profile. Please don't open public
issues for security reports.

We aim to acknowledge within 72 hours and to ship a fix or mitigation
within 30 days.
