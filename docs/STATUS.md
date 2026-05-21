# Omega — Development Status

## Current Milestone: M2 — First Playback (`v0.1.0-alpha`)

Goal: add a note to a track via the C API, press play, and have it fire at the right tick
via a `CapturingSink`. No real MIDI output.

| Sprint | Status | Description |
|--------|--------|-------------|
| M2.1 | ✅ Done | ClockSource + InternalClock + MockClock |
| M2.2 | ✅ Done | OutputSink + CapturingSink |
| M2.3 | ✅ Done | TimelineSource + TrackData |
| M2.4 | 🔄 In progress | C API wiring |

---

## Completed Milestones

| Milestone | Tag | Description |
|-----------|-----|-------------|
| M0 | — | Infrastructure (done 2026-05-20) |
| M1 | — | Core engine skeleton (done 2026-05-20) |

See `docs/ROADMAP.md` for full sprint specs and definitions of done.
