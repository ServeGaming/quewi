# Getting to quewi 1.0

Written 2026-07-17. The honest version — what's actually blocking, not a
feature wishlist.

> **Outcome, same day:** Matthew chose to ship as-is — "release it as is and
> have patches as we go along." v1.0.0 was tagged with gates 3 and 4 knowingly
> open; the reasoning and consequences are recorded in
> `session-handoff.md` (§ "1.0 shipped"). This document stays as the record of
> what the gates were and why they mattered — gates 3 and 4 are now the 1.0.1
> work list.

## What 1.0 means here

1.0 is a promise: *"this is stable enough to run a real show on."* Someone will
open a paying performance with it. That's the bar every decision below is
measured against — not "does the feature exist" but "will it embarrass an
operator in front of an audience."

The infrastructure to ship is already built and proven: `release.yml` produces
Windows MSI, macOS DMG (universal), and Linux AppImage from a `v*` tag, and it
degrades gracefully when no signing cert is configured. Pushing a `v1.0.0` tag
would produce installers today. The question is whether we *should* — and the
answer is not yet, for reasons that are about correctness, not polish.

## The gates — nothing tagged 1.0 until these are true

### 1. The mix grid works in the real app

It's tested and it compiles, but it has **never been driven in the running
app** — create a list, add cues, type an assignment, watch the highlighting,
save, reopen. Unit tests prove the model; they don't prove the wiring. This is
first because nothing else matters if it's broken, and because we just shipped
it as the headline feature.

Blocked on: nobody. Just needs a driving session.

### 2. The channel/ensemble editor exists

`MixShow` holds channels, actors, backups and ensembles — all persisted, with
**no UI**. Right now the only way to name a mic is to hand-edit the show file.
The grid is genuinely unusable without this, so despite being "phase 3" in the
original spec it's really part of shipping the grid at all. A 1.0 that advertises
DCA mixing but requires a text editor to name a channel is not 1.0.

Blocked on: nobody.

### 3. The Windows updater actually installs

Standing bug from before the mix work: a user on 0.9.103 reported *"download
bar, then quewi just closes, nothing installs."* An auto-updater that closes the
app without updating is worse than none — it looks like a crash and loses the
user's confidence in every future update. Client step-logging shipped; it needs
one user run to produce `%APPDATA%/quewi/update-client.log`.

Blocked on: **the user**, running the failing updater once so we get the log.
This is the single highest-value thing you can do that I can't.

### 4. A real end-to-end pass on the things that ship unproven

The mix engine is unit-tested against fakes, not hardware. Before 1.0, at least
one of the two protocols should be driven against a real desk (or the emulator
for X32). We don't need both — but shipping "controls your console" 1.0 with
zero real-console runs is a claim we can't stand behind.

Blocked on: the DM7 hardware session (user) OR an X32-Edit/emulator session
(me). The X32 emulator path needs no hardware and I can do it — that's the
cheaper way to clear this gate.

## Not gates — explicitly deferred past 1.0

Being clear about these so they don't creep into the critical path:

- **Signing certificates.** Installers ship unsigned/ad-hoc. Users get a
  SmartScreen / Gatekeeper warning and click through. That's a real papercut,
  but it's a *money* problem (paid certs) and a known one, documented in
  `docs/dev/release-signing.md`. 1.0 can ship unsigned; 1.1 signs. Don't let
  this block the release.
- **DM7 EQ / channel profiles (phase 4).** Blocked on the PEQ-gain-scaling
  hardware test, and not needed for the DCA-cue core that is the actual product.
- **The fader surface, positions, FX assignments, level offsets.** Phases 5–7.
  TheatreMix parity is a long road; 1.0 is the DCA spine done well, not the
  whole map.
- **Light-theme tokenisation.** Cosmetic debt, dark theme is the default.

## The sequence

```
now ──► [1] drive the grid ──► [2] channel/ensemble editor ──► [1] drive it again, for real
                                                                      │
        [3] updater log (user) ───────────────────────────────────┐  │
        [4] X32 emulator end-to-end (me) ─────────────────────────┤  │
                                                                   ▼  ▼
                                              release-candidate: tag v1.0.0-rc1
                                                                      │
                                          installers build, smoke-test each platform
                                                                      │
                                                     stress test (user) ──► fix ──► v1.0.0
```

The `-rc1` tag is the point of no cheating: it runs the real release pipeline
and produces the actual installers, so we test what ships rather than what
builds locally. If rc1 installs clean on all three platforms and survives a
stress pass, it becomes 1.0 with a re-tag.

## What I need from you, concretely

Two things only I can't do:

1. **Run the broken updater once** (gate 3) and send me
   `%APPDATA%/quewi/update-client.log`.
2. **Get on the DM7** at some point (gate 4 alternative, and unblocks phase 4
   later) — `tools/dm7_probe.py <IP>`.

Everything else on the critical path — driving the grid, the channel editor,
the X32 emulator pass — I can do without you. Say go and I start on the channel
editor, since it's the thing gate 1 is waiting for.
