# Migration Guide: v0.x → v1.0

This document covers what changed between the v0.x pre-release series and the v1.0.0
stable release, and how to update consuming projects.

---

## What v1.0.0 Means

**The C API (`omega.h`) is now ABI-stable within a MAJOR version.**

Binaries compiled against v1.0.0 are binary-compatible with any v1.x.y release without
recompilation. Anthropic's versioning promise:

| Change type | Version bump |
|---|---|
| New public C API function (old API unchanged) | MINOR |
| Bug fix with no API change | PATCH |
| Any ABI break (struct layout, symbol signature) | MAJOR |
| Any C API removal | MAJOR |

The C++ internal API (`omega::core`, everything under `include/omega/` except `omega.h`)
carries **no** ABI or API stability guarantee between any releases. Always rebuild
consumers when the minor or patch version changes if you use the C++ API directly.

---

## Breaking Changes

There are **no breaking changes** from v0.6.0-beta to v1.0.0.

The v0.x series was a pre-release development cycle. No external consumers should have
been shipping against it; the v1.0.0 tag is the first stable baseline.

If you were tracking the `main` branch during development, the only required change is
bumping the version you declare in your `FetchContent` or `find_package` call:

```cmake
# Before (tracking pre-release)
FetchContent_Declare(omega GIT_TAG v0.6.0-beta ...)

# After (stable)
FetchContent_Declare(omega GIT_TAG v1.0.0 ...)
```

---

## New in v1.0.0 vs Earlier Pre-releases

| Feature | Introduced in |
|---|---|
| Core engine, SPSC queue, TempoMap | v0.1.0 |
| TimelineSource, C API wiring, smoke test | v0.2.0 |
| PatternLibrary, SongArrangementSource, PerformanceSource (64 slots) | v0.3.0 |
| EventInput / InputBus, ModulationBus, PerformanceContext, TransformSource | v0.4.0 |
| TimeSignatureMap, MeterCursor, SmpteConverter, `OMEGA_CUE_BAR` | v0.4.0 |
| Real MIDI I/O (libremidi), SMF import/export (midifile), OmegaTimer | v0.5.0-alpha |
| MarkerList, RegionList, AnchorPoint, EventAnchorTable, snap framework | v0.5.x |
| ≥ 80 % test coverage, ASan/TSan clean, ABI baseline, benchmark suite | v0.6.0-beta |
| Stable ABI, complete API documentation, v1.0.0 | **v1.0.0** |

---

## Known Limitations

- **`omega::core` (C++ API) is not ABI-stable.** Only the C API (`omega.h`) is covered
  by the stability guarantee. If you link against the C++ types directly, expect to
  rebuild on every release.
- **PMR allocators** are wired through the C++ API only. The C API always uses the
  heap default allocator.
- **Ableton Link** (`OMEGA_WITH_LINK=ON`) changes the combined license to GPL v2+. See
  [docs/design/07-extensions.md](../design/07-extensions.md).
- **SMPTE helpers** (`SmpteConverter`, `omega_tick_to_smpte()`) require a configured
  `SmpteConfig`; they return `OMEGA_ERR_NO_SMPTE_CONFIG` if called without one.
- **Meter helpers** (`MeterCursor`, `omega_bar_beat_to_tick()`) require at least one
  `TimeSignatureMap` entry; they return `OMEGA_ERR_NO_METER` in freeform mode.
