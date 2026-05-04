# Performance Budgets

quewi must feel fast. These budgets are first-class design properties (see `design.md §2`) and are asserted in CI where automatable.

## Reference machine

CI gates run on `ubuntu-latest` GitHub-hosted runner. Local dev numbers will vary. The numbers below are the targets for the reference runner; manual smoke checks on a 2020-era developer laptop should be at parity or better.

## Budgets

| Metric | Target | Enforcement |
|---|---|---|
| Cold start to usable UI | < 500 ms | CI smoke timer |
| Show file load (200 cues) | < 200 ms | Phase 1 benchmark |
| Idle CPU (loaded show, idle) | < 0.5 % / core | Manual |
| Idle RAM | < 150 MB resident | CI memory probe |
| GO press → first audio sample | < 5 ms median, < 15 ms p99 | Phase 3 latency rig |
| GO press → first OSC byte on wire | < 2 ms median | Phase 2 wire probe |
| Cue list scroll (10 000 cues) | 60 fps | Phase 1 benchmark |
| Main executable size | < 30 MB (excluding FFmpeg) | CI binary-size check |
| Background threads when idle | 0 (besides audio device callback) | Manual |

## Architectural rules

These follow from the budgets and are enforced in code review:

1. Lazy-init every subsystem; don't open audio/MIDI devices until a cue references them.
2. Virtualized list views — Qt's `QAbstractItemModel` viewport-only rendering.
3. Avoid Qt Quick/QML in hot UI paths.
4. Lock-free SPSC ring buffers between UI thread and engine threads. **No mutexes on the audio path.**
5. Pre-decode the next cue's audio head during pre-roll, not at GO time.
6. No allocations in the audio device callback.
7. No string formatting in the audio or OSC send hot paths.

## Measuring

- **Cold start:** wall time from `main()` entry to `QMainWindow::show()` returning.
- **GO latency:** RDTSC at GO press → wire-probe / loopback timestamp.
- **Idle CPU/RAM:** `pidstat -r -u` over 30 s with the app idle on a loaded show.
- **Binary size:** `ls -l` on the linked executable; Linux strip applied for the gate.

CI probes added incrementally as each phase's subsystem comes online.
