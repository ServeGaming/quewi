# License

Quewi is licensed under the **GNU Affero General Public License,
version 3** ([AGPL-3.0](https://www.gnu.org/licenses/agpl-3.0.html)).
Full text in the [LICENSE file](https://github.com/ServeGaming/quewi/blob/main/LICENSE)
in the repository root.

---

## What this means in plain English

**You can use it.**
For any purpose. Commercial, non-commercial, personal, in a
theatre, in your living room. No license fee, no royalties.

**You can modify it.**
Fork the source, change anything, build your own version.

**You can distribute it.**
Share binaries and source with anyone. Sell physical media
containing it.

**You CAN'T charge money for the software itself.**
Not directly. The AGPL doesn't *technically* forbid charging,
but any paid fork must publish full source under AGPL — which
means anyone who buys your paid version can republish it for
free immediately. The economics make paid distribution
impractical.

This was deliberate. quewi is intended to be free software for
the theatre community, not a freemium pipeline.

**You MUST publish your source if you distribute or run it as a
network service.**
The "A" in AGPL — Affero — means even running a modified version
as a service over a network (rather than distributing binaries)
triggers the source-disclosure requirement. If you fork quewi
and host an instance for paying customers, your fork's source
must be made available.

**You CAN'T re-license it.**
Modifications must stay under AGPL-3.0.

---

## What's bundled with the binary

Quewi statically links / dynamically links a few other open-source
projects:

| Component | License | Use |
|---|---|---|
| **Qt 6** | LGPL-3.0 | GUI framework, audio, networking, WebSockets, PDF |
| **FFmpeg** | LGPL-2.1+ (the LGPL build) | Video / audio decoding |
| **RtMidi** | MIT-modified | MIDI device I/O |
| **miniaudio** | MIT / 0BSD (dual) | Audio backend (planned fallback) |

LGPL components allow dynamic linking from non-LGPL code. We
ship them as separate dynamic libraries (`.dll` / `.dylib` /
`.so`) inside the bundle, so users can replace them if needed.

---

## Trademark

The name "quewi" and the kiwi icon are not trademarked. Forks
are welcome to use either; we ask (don't require) that you pick
a distinct name for your fork so users can tell the projects
apart.

---

## Compliance

If you redistribute quewi, you need to:

1. Include the LICENSE file (it's in the source tree and the
   installer drops it into the install dir on every platform).
2. Make the source code of *your version* available to recipients
   — either by including it in the distribution or by providing
   a written offer to provide it.
3. If you modify quewi, mark the changes (a `CHANGELOG.md` or
   git commits both satisfy this).

Practically: forking on GitHub and publishing builds from your
fork satisfies all three.

---

## Why AGPL specifically?

The strong copyleft + network-service clause is what makes the
"nobody charges for quewi" goal stick. Without the network
clause, a paid SaaS fork could keep modifications proprietary
and charge for hosted quewi. With it, even a SaaS deployment
must publish source.

GPL-2 / GPL-3 don't have the network clause. BSD / MIT / Apache
have neither the network clause nor the copyleft. AGPL is the
specific tool for this specific intent.

---

## Patent grant

AGPL-3 includes an explicit patent grant: by contributing or
distributing, you grant a patent license for any patents you
hold that read on your contributions. Quewi has no known
patentable surface (it's an integration of established
protocols), but the grant is in place anyway.
