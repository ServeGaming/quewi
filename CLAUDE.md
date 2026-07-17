# CLAUDE.md — read on every session start

## ⭐ First thing, every session: read the handoff

**`docs/dev/session-handoff.md` is the living state of this project** — what's
built, what's in flight, what's blocked, what's next, and the decisions behind
it all. Read it before doing anything else so you continue with no gaps from the
last session (which may have been on a different computer).

**Then keep it current.** When you finish a meaningful chunk of work, update the
handoff, commit it, and `git push origin main`. Do it at checkpoints — not every
message, but often enough that if this session ended right now, the next one
would lose nothing. The handoff's *Update protocol* section is the how. This is
not optional; it's the mechanism that lets Matthew switch machines and pick up
instantly.

## The project in one paragraph

quewi: a Qt 6 / C++23 theatre cueing app (github.com/ServeGaming/quewi), aiming
at QLab + TheatreMix parity. The active thread is **quewi Mix** — a TheatreMix
clone (live DCA console mixing) built inside the app. Full context is in the
handoff and the docs it points to (`docs/dev/`).

## Build & test (details and gotchas in the handoff)

- Local Qt: `C:\Qt\6.11.0\msvc2022_64`. Build needs MSVC vcvars; tests need Qt on PATH.
- Build: `cmake --build --preset windows-release` (after vcvars64.bat).
- Test: `ctest` from `build/windows-release` with Qt's `bin` on PATH; and
  `quewi.exe --selftest` must exit 0.
- All 18 test suites must stay green. Verify a change actually works by driving
  the app, not just compiling — "built but not driven" is not "done".

## Working style Matthew has asked for

- Ship each change as a version bump: commit, push. Branch off `main` isn't
  required — he works on `main`.
- Be honest about state: what's tested vs untested, what's blocked on him
  (he must run the Windows updater once, and get on the DM7). Don't overclaim.
- Design/theme work goes to Fable 5 (Agent tool, `model: fable`) as a background
  task with strict file boundaries. Don't rotate the signing secret; it's fine.
