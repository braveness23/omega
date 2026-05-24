# Omega v0.5.0 through v1.0.0 Implementation Plan

## Context

Omega is at v0.4.0 (released 2026-05-23) with M0-M4.5 complete: core engine, all 3 playback sources, orchestration layer, time signatures, SMPTE, CUE_BAR. CMakeLists.txt VERSION is already 0.5.0. The remaining work covers real MIDI I/O, SMF import/export, markers/regions/anchors (approved design doc 14), polish, and the stable v1.0.0 release. This plan is structured as 10 PRs that Claude Sonnet 4.6 can execute sequentially, each self-contained with explicit file lists, C API signatures, and CI guardrails.

---

## Formatting & CI Rules (Apply to EVERY PR)

These rules prevent the pipeline failures that have repeatedly bitten this project. **Read this section before starting any PR.**

1. **Always run `clang-format-18`** (not 17, not 19). Command:
   ```bash
   find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
     | xargs clang-format-18 -i
   ```
2. **Use `<cstdint>` never `<stdint.h>`** in all C++ files.
3. **Include all headers explicitly** -- Apple Clang does not provide transitive includes that GCC does. If you use `std::string`, include `<string>`. If you use `std::vector`, include `<vector>`. Every file must compile standalone.
4. **No Unicode in Catch2 TEST_CASE names** -- no em dashes, smart quotes, or non-ASCII. Use ASCII hyphens (`-` or `--`) instead.
5. **Initialize all local variables before `memcpy`** -- MSVC Debug `/RTCu` crashes on uninitialized reads.
6. **clang-tidy needs `-p build_tidy`** -- rebuild `build_tidy` after adding any new source file:
   ```bash
   cmake -B build_tidy -DOMEGA_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build_tidy
   ```
7. **Run `bash scripts/local-ci.sh` before every push.** This mirrors the three cheapest CI checks (format, tidy, CHANGELOG).
8. **CHANGELOG rule**: every `feat:` or `fix:` PR must update CHANGELOG.md under `[Unreleased]`. Use `chore:` or `docs:` prefix to skip.
9. **`Command` variant must remain <= 64 bytes** -- verify new command types with `static_assert(sizeof(Command) <= 64)`.
10. **C API casts** in `omega_c.cpp` need `// NOLINT` for `cppcoreguidelines-pro-type-reinterpret-cast` where unavoidable.

### Pre-push checklist (copy-paste for each PR)

```bash
# 1. Format
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-18 -i

# 2. Rebuild build_tidy (if new source files added)
cmake -B build_tidy -DOMEGA_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_tidy

# 3. Run local CI
bash scripts/local-ci.sh

# 4. Dev build + test
cmake --preset dev && cmake --build --preset dev && ctest --preset dev

# 5. TSan build + test
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

---

## PR 1 / Sprint 5.1 -- libremidi MIDI I/O

**Branch:** `feat/m5.1-midi-io`
**Status: COMPLETE** (merged pending) — 2026-05-24

### What was delivered

All planned items were implemented. The following were added beyond the original spec:

- **Pitch bend, aftertouch, poly AT**: `OMEGA_PITCH_BEND` (0x04), `OMEGA_AFTERTOUCH` (0x05), and `OMEGA_POLY_AT` (0x06) were already in the C++ `PayloadTag` enum but had no C API constants and were silently dropped by both `event_to_midi_bytes()` and `midi_bytes_to_event()`. All three were added with full round-trip serialization and 3 new tests. Pitch bend stores LSB in `data[0]`, MSB in `data[1]` (wire order).
- **CI fix** (`-DLIBREMIDI_NO_ALSA=ON`): on headless runners, `snd_seq_open` returns NULL and libremidi's internal `assert(seq)` fires SIGABRT — uncatchable by try/catch. ALSA backend disabled at compile time for all three Linux CI jobs; `libasound2-dev` kept for headers.
- **`.gitignore`**: `build*/`, `_deps/`, `.cache/` added.
- **Design doc 14** (`docs/design/14-markers-regions-anchors.md`): markers/regions/anchor points design committed on its own commit.

### Files created

- `include/omega/midi_io.h`
- `src/libremidi_sink.cpp`
- `src/libremidi_input.cpp`
- `tests/unit/test_midi_io.cpp`
- `.gitignore`
- `docs/design/14-markers-regions-anchors.md`

### Files modified

- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `include/omega/omega.h` — `omega_sink_create_midi_out()`, `omega_input_create_midi_in()`, `OMEGA_ERR_IO = -8`, `OMEGA_PITCH_BEND`, `OMEGA_AFTERTOUCH`, `OMEGA_POLY_AT`
- `src/omega_c.cpp`
- `.github/workflows/ci.yml` — ALSA CI fix
- `CHANGELOG.md`

### Test count: 15 cases, 38 assertions (all `[midi_io]`)

---

## PR 2 / Sprint 5.2 -- MarkerList + RegionList + SMF Import

**Branch:** `feat/m5.2-smf-import-markers`

### Files to create

- `include/omega/marker_list.h` -- `Marker{name, tick}`, `MarkerList`
- `include/omega/region_list.h` -- `Region{name, start_tick, end_tick, type}`, `RegionList`, `OMEGA_REGION_LOOP/PUNCH/SECTION`
- `src/marker_list.cpp`
- `src/region_list.cpp`
- `src/smf_import.cpp`
- `tests/unit/test_marker_list.cpp`
- `tests/unit/test_region_list.cpp`
- `tests/unit/test_smf_import.cpp`
- `tests/fixtures/` -- directory for test .mid files (generate programmatically in tests or commit small binaries)

### Files to modify

- `CMakeLists.txt` -- add `src/marker_list.cpp`, `src/region_list.cpp`, `src/smf_import.cpp` to `omega_core`; link `midifile` library
- `tests/CMakeLists.txt` -- add three new test files to `omega_tests`
- `include/omega/engine.h` -- add `MarkerList& marker_list()`, `RegionList& region_list()` accessors + private members
- `src/engine.cpp` -- initialize marker_list_ and region_list_
- `include/omega/omega.h` -- add C API for smf_import, markers, regions
- `src/omega_c.cpp` -- implement C API wrappers
- `CHANGELOG.md`

### Implementation details

**MarkerList**: sorted `std::vector<Marker>` by tick. Methods: `add(name, tick)`, `remove(index)`, `at(index)`, `clear()`, `find_nearest(tick)` (returns marker at or before tick), `size()`, `points()` (const ref for serialization).

**RegionList**: sorted `std::vector<Region>` by start_tick. Region types:
```cpp
enum RegionType : uint8_t { REGION_LOOP = 0, REGION_PUNCH = 1, REGION_SECTION = 2 };
```
Methods: `add(name, start, end, type)` (validates start < end), `remove(index)`, `at(index)`, `clear()`, `find_containing(tick)`, `size()`.

**omega_smf_import()** -- mutation-thread operation (engine must be stopped):
1. `smf::MidiFile mf; mf.read(path);` -- return `OMEGA_ERR_IO` on failure
2. `mf.makeAbsoluteTicks();` -- normalize to absolute ticks
3. Tick scaling: `smf_ppqn = mf.getTPQ()`. For each tick: `omega_tick = (smf_tick * OMEGA_PPQN) / smf_ppqn` (integer math)
4. Per track: create omega track, iterate events:
   - `isTempo()`: extract usec/beat, convert to `bpm_milli = 60'000'000'000ULL / usec_per_beat`, insert into `TempoMap`
   - `isTimeSignature()`: extract num, raw denom exponent, convert `denom = 1 << raw_denom`, insert into `TimeSignatureMap`
   - Meta type `0x06` (Marker): add to `MarkerList` at scaled tick
   - Meta type `0x07` (Cue Point): add to `MarkerList` at scaled tick (prefix name with "[cue] ")
   - Note-on (`0x90`, vel > 0): use midifile's linked note-off to compute duration, create event
   - CC (`0xB0`): create CC event
   - Program (`0xC0`): create program event
5. Return `OMEGA_OK`

### Tests

**test_marker_list.cpp** (~8 cases):
- Add markers, verify sorted by tick
- Remove by index
- Clear empties list
- find_nearest returns correct marker
- find_nearest on empty list returns null
- Duplicate ticks allowed (different names)
- Size tracking

**test_region_list.cpp** (~8 cases):
- Add regions, verify sorted by start_tick
- Remove by index
- Validate start < end (returns error)
- find_containing returns correct regions
- Overlapping regions both returned
- Clear empties list

**test_smf_import.cpp** (~6 cases):
- Import Type 0: verify track count == 1, first note at correct tick
- Import Type 1: verify track count matches, tempos imported
- Import non-480 PPQN: verify tick scaling (e.g., 240 PPQN file, tick 240 -> omega tick 480)
- Import with markers: verify MarkerList populated
- Import with time signatures: verify TimeSignatureMap populated
- Import failure: bad path returns OMEGA_ERR_IO

### C API additions

```c
typedef struct { const char* name; omega_tick_t tick; } omega_marker_t;
typedef struct { const char* name; omega_tick_t start_tick; omega_tick_t end_tick; uint8_t type; } omega_region_t;

#define OMEGA_REGION_LOOP    0
#define OMEGA_REGION_PUNCH   1
#define OMEGA_REGION_SECTION 2

omega_status_t omega_smf_import(omega_engine_t* e, const char* path);

omega_status_t omega_marker_add(omega_engine_t* e, const char* name, omega_tick_t tick);
omega_status_t omega_marker_remove(omega_engine_t* e, uint32_t index);
uint32_t       omega_marker_count(const omega_engine_t* e);
omega_status_t omega_marker_at(const omega_engine_t* e, uint32_t index, omega_marker_t* out);
omega_status_t omega_marker_clear(omega_engine_t* e);

omega_status_t omega_region_add(omega_engine_t* e, const char* name,
                                omega_tick_t start, omega_tick_t end, uint8_t type);
omega_status_t omega_region_remove(omega_engine_t* e, uint32_t index);
uint32_t       omega_region_count(const omega_engine_t* e);
omega_status_t omega_region_at(const omega_engine_t* e, uint32_t index, omega_region_t* out);
omega_status_t omega_region_clear(omega_engine_t* e);
```

### CHANGELOG

```markdown
### Added
- **Sprint 5.2 -- SMF import + markers + regions**: `omega_smf_import()` reads Standard MIDI Files (Type 0/1, tempo, time signature, markers, cue points, non-480 PPQN scaling); `MarkerList` and `RegionList` as Session-level data; full C API for marker/region CRUD
```

### Commit & PR

```bash
git checkout -b feat/m5.2-smf-import-markers
# ... implement, format, test ...
git commit -m "feat: add SMF import, MarkerList, and RegionList (sprint 5.2)"
git push -u origin feat/m5.2-smf-import-markers
gh pr create --title "feat: SMF import + markers/regions (M5.2)" --body "..."
```

---

## PR 3 / Sprint 5.3 -- SMF Export

**Branch:** `feat/m5.3-smf-export`

### Files to create

- `src/smf_export.cpp`
- `tests/unit/test_smf_export.cpp`

### Files to modify

- `CMakeLists.txt` -- add `src/smf_export.cpp`
- `tests/CMakeLists.txt` -- add `unit/test_smf_export.cpp`
- `include/omega/omega.h` -- add `omega_smf_export()`
- `src/omega_c.cpp` -- implement wrapper
- `CHANGELOG.md`

### Implementation details

`omega_smf_export(omega_engine_t* e, const char* path, int smf_type)`:
1. `smf::MidiFile mf; mf.setTPQ(OMEGA_PPQN);`
2. If `smf_type == 0`, merge all tracks into track 0. If `smf_type == 1`, one midifile track per omega track, plus track 0 for tempo/meta.
3. Export tempo map: iterate `tempo_map_.points()`, compute `usec_per_beat = 60'000'000'000ULL / bpm_milli`, add tempo event
4. Export time signatures: iterate `timesig_map_.points()`, compute `denom_exponent = log2(denominator)`, add time sig event
5. Export markers: iterate `marker_list_.points()`, add marker meta-events (FF 06)
6. Per track: iterate events, convert NOTE_ON to note-on + note-off pair, CC, PROGRAM
7. `mf.sortTracks(); mf.write(path);`
8. Return `OMEGA_OK` or `OMEGA_ERR_IO`

### Tests

- Round-trip Type 1: create engine, add 2 tracks with events, export, re-import, verify match
- Round-trip Type 0: single track
- Round-trip with tempo changes: 3 tempo changes survive
- Round-trip markers: add markers, export, re-import, verify
- Round-trip time signatures: verify preservation

### C API

```c
omega_status_t omega_smf_export(omega_engine_t* e, const char* path, int smf_type);
```

### Commit & PR

```bash
git checkout -b feat/m5.3-smf-export
git commit -m "feat: add SMF export with marker round-trip (sprint 5.3)"
git push -u origin feat/m5.3-smf-export
gh pr create --title "feat: SMF export (M5.3)" --body "..."
```

---

## PR 4 / Sprint 5.4 -- OmegaTimer

**Branch:** `feat/m5.4-omega-timer`

### Files to create

- `include/omega/timer.h`
- `src/timer.cpp`
- `tests/unit/test_timer.cpp`

### Files to modify

- `CMakeLists.txt` -- add `src/timer.cpp`
- `tests/CMakeLists.txt` -- add `unit/test_timer.cpp`
- `include/omega/omega.h` -- add `omega_timer_create()`, `omega_timer_destroy()`
- `src/omega_c.cpp` -- implement timer C API
- `CHANGELOG.md`

### Implementation details

```cpp
class OmegaTimer
{
public:
    explicit OmegaTimer(Engine& engine, uint32_t interval_us = 1000);
    ~OmegaTimer();

    OmegaTimer(const OmegaTimer&) = delete;
    OmegaTimer& operator=(const OmegaTimer&) = delete;
    OmegaTimer(OmegaTimer&&) = delete;
    OmegaTimer& operator=(OmegaTimer&&) = delete;

private:
    void run();
    Engine& engine_;
    uint32_t interval_us_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
```

Use `std::this_thread::sleep_for(std::chrono::microseconds(interval_us_))` for portability. Destructor: set `stop_ = true`, `thread_.join()`, call `engine_.process()` one final time.

Include `<thread>`, `<atomic>`, `<chrono>`.

### Tests

- `OmegaTimer: process is called during lifetime` -- MockClock, add note at tick 0, create timer, sleep 10ms, verify CapturingSink received note
- `OmegaTimer: destructor joins cleanly` -- construct/destruct 100 times in a loop, no hangs
- `OmegaTimer: concurrent mutation is TSan clean` -- timer + enqueue from another thread

### C API

```c
typedef struct omega_timer_s omega_timer_t;
omega_timer_t* omega_timer_create(omega_engine_t* e, uint32_t interval_us);
void           omega_timer_destroy(omega_timer_t* timer);
```

### Post-merge: Tag v0.5.0-alpha

After this PR merges to main:

```bash
git checkout main && git pull
git tag -a v0.5.0-alpha -m "M5 complete -- real MIDI I/O and SMF import/export"
git push origin v0.5.0-alpha
```

Then create GitHub Release with the CHANGELOG `[0.5.0-alpha]` section.

### Commit & PR

```bash
git checkout -b feat/m5.4-omega-timer
git commit -m "feat: add OmegaTimer RAII thread wrapper (sprint 5.4)"
git push -u origin feat/m5.4-omega-timer
gh pr create --title "feat: OmegaTimer (M5.4)" --body "..."
```

---

## PR 5 / Version Bump to 0.6.0

**Branch:** `chore/bump-v0.6.0`

### Files to modify

- `CMakeLists.txt` -- `VERSION 0.5.0` -> `VERSION 0.6.0`
- `CHANGELOG.md` -- promote `[Unreleased]` to `[0.5.0-alpha] - YYYY-MM-DD`, add new empty `[Unreleased]` section, update comparison links at bottom:
  ```markdown
  [Unreleased]: https://github.com/braveness23/omega/compare/v0.5.0-alpha...HEAD
  [0.5.0-alpha]: https://github.com/braveness23/omega/compare/v0.4.0...v0.5.0-alpha
  ```
- `docs/STATUS.md` -- update current milestone to M5.5
- `docs/ROADMAP.md` -- mark M5 as done

### Commit & PR

```bash
git checkout -b chore/bump-v0.6.0
git commit -m "chore: bump VERSION to 0.6.0 after v0.5.0-alpha tag"
git push -u origin chore/bump-v0.6.0
gh pr create --title "chore: bump to 0.6.0" --body "Post-v0.5.0-alpha version bump"
```

No CHANGELOG lint needed (chore: prefix).

---

## PR 6 / Sprint 5.5.1 -- AnchorPoint + Pattern Anchors + Event Side Table

**Branch:** `feat/m5.5-anchors`

### Files to create

- `include/omega/anchor_point.h` -- `AnchorPoint{name, offset_ticks, flags}`, flag constants, `AnchorList` class
- `src/anchor_point.cpp` -- `AnchorList` methods
- `include/omega/event_anchor_table.h` -- `EventAnchorTable` sparse side table
- `src/event_anchor_table.cpp`
- `tests/unit/test_anchor_point.cpp`
- `tests/unit/test_event_anchor_table.cpp`

### Files to modify

- `CMakeLists.txt` -- add `src/anchor_point.cpp`, `src/event_anchor_table.cpp`
- `tests/CMakeLists.txt` -- add two new test files
- `include/omega/pattern.h` -- add `AnchorList anchors;` member to `Pattern`
- `include/omega/engine.h` -- add `EventAnchorTable& event_anchors()` accessor + private member
- `src/engine.cpp` -- initialize event_anchors_
- `include/omega/omega.h` -- add anchor C API
- `src/omega_c.cpp` -- implement anchor C API
- `CHANGELOG.md`

### Implementation details

**AnchorPoint** (from design doc 14):
```cpp
struct AnchorPoint
{
    std::string name;
    uint64_t offset_ticks;
    uint32_t flags;
};
```

Flag constants:
```cpp
constexpr uint32_t OMEGA_ANCHOR_SNAP = 0x01u;
constexpr uint32_t OMEGA_ANCHOR_WARP = 0x02u;
constexpr uint32_t OMEGA_ANCHOR_CUE  = 0x04u;
```

**AnchorList**: stores `std::vector<AnchorPoint>` sorted by `offset_ticks`. Members:
- `add(name, offset_ticks, flags)` -- insert sorted
- `remove(name)` -- remove by name
- `at(index)` -> `const AnchorPoint*`
- `find_by_name(name)` -> `const AnchorPoint*`
- `snap_anchors()` -> filtered view of ANCHOR_SNAP items
- `set_active_snap(index)` -- must have ANCHOR_SNAP flag, returns OMEGA_ERR_INVALID otherwise
- `active_snap()` -> `const AnchorPoint*` or nullptr
- `size()`, `clear()`

**EventAnchorTable**: `std::unordered_map<uint64_t, AnchorList>` where key = `(container_id << 16) | event_index`. Sparse -- only events with anchors have entries. This keeps events at 24 bytes per design doc 14.

**Pattern anchors**: `Pattern::anchors` stores intrinsic anchors that travel with the pattern. These are per-pattern, not per-placement.

### Tests

**test_anchor_point.cpp** (~8 cases):
- Add and retrieve (sorted by offset)
- Remove by name
- Active snap anchor set/get
- Active snap requires ANCHOR_SNAP flag (error case)
- snap_anchors() filtered view
- Combinable flags (SNAP | CUE)
- Empty list edge cases
- find_by_name on nonexistent returns null

**test_event_anchor_table.cpp** (~4 cases):
- Add and retrieve for event
- Missing key returns null
- Remove entry
- Multiple events with independent anchor lists

### C API additions

```c
#define OMEGA_ANCHOR_SNAP 0x01u
#define OMEGA_ANCHOR_WARP 0x02u
#define OMEGA_ANCHOR_CUE  0x04u

omega_status_t omega_pattern_add_anchor(omega_engine_t* e, omega_pattern_id_t pid,
                                        const char* name, omega_tick_t offset, uint32_t flags);
omega_status_t omega_pattern_remove_anchor(omega_engine_t* e, omega_pattern_id_t pid,
                                           const char* name);
uint32_t       omega_pattern_anchor_count(const omega_engine_t* e, omega_pattern_id_t pid);
omega_status_t omega_pattern_set_active_snap(omega_engine_t* e, omega_pattern_id_t pid,
                                             uint32_t index);

omega_status_t omega_event_add_anchor(omega_engine_t* e, omega_track_id_t track,
                                      uint32_t event_index, const char* name,
                                      omega_tick_t offset, uint32_t flags);
omega_status_t omega_event_remove_anchor(omega_engine_t* e, omega_track_id_t track,
                                         uint32_t event_index, const char* name);
```

### Commit & PR

```bash
git checkout -b feat/m5.5-anchors
git commit -m "feat: add AnchorPoint type, pattern anchors, and event side table (sprint 5.5.1)"
git push -u origin feat/m5.5-anchors
gh pr create --title "feat: anchors + event side table (M5.5.1)" --body "..."
```

---

## PR 7 / Sprint 5.5.2 -- Snap Framework

**Branch:** `feat/m5.5-snap-framework`

### Files to create

- `include/omega/snap.h` -- `SnapTarget` enum, `SnapConfig`, `SnapResult`, `snap_to_nearest()`
- `src/snap.cpp`
- `tests/unit/test_snap.cpp`

### Files to modify

- `CMakeLists.txt` -- add `src/snap.cpp`
- `tests/CMakeLists.txt` -- add `unit/test_snap.cpp`
- `include/omega/omega.h` -- add snap C API
- `src/omega_c.cpp` -- implement snap C API
- `CHANGELOG.md`

### Implementation details

```cpp
enum class SnapTarget : uint8_t
{
    GRID     = 0x01,
    MARKERS  = 0x02,
    REGIONS  = 0x04,
    ANCHORS  = 0x08,
};

struct SnapConfig
{
    uint8_t targets;              // bitfield of SnapTarget
    uint64_t grid_subdiv_ticks;   // subdivision for grid snap (0 = auto from PositionConverter)
    uint64_t tolerance_ticks;     // max snap distance (0 = unlimited)
};

struct SnapResult
{
    uint64_t snapped_tick;
    SnapTarget source;
    bool did_snap;
};
```

Core algorithm from design doc 14:
```
snap_target  = nearest_grid_point(item_start + active_anchor.offset_ticks)
item_start   = snap_target - active_anchor.offset_ticks
```

`snap_to_nearest(tick, config, converter, marker_list, region_list, external_anchors)`:
1. Collect candidates from enabled targets:
   - `GRID`: `converter.next_boundary()` and previous boundary
   - `MARKERS`: iterate markers, find nearest tick
   - `REGIONS`: collect all start/end ticks, find nearest
   - `ANCHORS`: iterate external anchors, find nearest offset
2. Pick candidate with minimum `|candidate - tick|`
3. If tolerance > 0 and distance exceeds it, return `{tick, _, false}`
4. Return `{nearest, source_type, true}`

Accepts `PositionConverter&` -- works with MeterCursor or SmpteConverter.

### Tests (~8 cases)

- Snap to beat grid (4/4 at 480 PPQN, tick 230 -> 240)
- Snap to bar boundary (tick 1900 -> 1920)
- Snap to nearest marker
- Snap to region boundary
- Snap with anchor offset (item at 100, anchor offset 40, grid at 480: result = 440)
- Tolerance exceeded returns original tick
- Combined targets (GRID | MARKERS), closest wins
- Freeform mode with GRID target returns OMEGA_ERR_NO_METER

### C API

```c
#define OMEGA_SNAP_GRID    0x01u
#define OMEGA_SNAP_MARKERS 0x02u
#define OMEGA_SNAP_REGIONS 0x04u
#define OMEGA_SNAP_ANCHORS 0x08u

typedef struct
{
    uint8_t targets;
    omega_tick_t grid_subdiv_ticks;
    omega_tick_t tolerance_ticks;
} omega_snap_config_t;

typedef struct
{
    omega_tick_t snapped_tick;
    uint8_t source;
    int did_snap;
} omega_snap_result_t;

omega_status_t omega_snap(const omega_engine_t* e, omega_tick_t tick,
                          const omega_snap_config_t* config, omega_snap_result_t* out);
```

### Commit & PR

```bash
git checkout -b feat/m5.5-snap-framework
git commit -m "feat: add snap framework with grid, marker, region, and anchor targets (sprint 5.5.2)"
git push -u origin feat/m5.5-snap-framework
gh pr create --title "feat: snap framework (M5.5.2)" --body "..."
```

---

## PR 8 / Sprint 6.1 -- Coverage to 80%

**Branch:** `feat/m6.1-coverage`

### Approach

Run coverage locally to find gaps:
```bash
cmake -B build_cov -DCMAKE_BUILD_TYPE=Debug -DOMEGA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build_cov
ctest --test-dir build_cov
gcovr --root . --gcov-executable gcov-12 --exclude-directories '.*_deps.*' \
      --print-summary build_cov
```

### Files to modify

Add test cases to existing files and/or create new test files based on coverage gaps. Expected areas:

- `tests/unit/test_tempo_map.cpp` -- empty map, single point, 100+ tempo changes, extreme BPM
- `tests/unit/test_spsc_queue.cpp` -- exactly full (4096), boundary wrap, pop from empty, push to full
- `tests/unit/test_performance_source.cpp` -- hard-to-trigger transitions (QUEUED->IDLE via unassign)
- `tests/unit/test_engine.cpp` -- null handles, invalid IDs, queue full
- `tests/unit/test_types.cpp` -- all `omega_status_string()` branches
- `tests/unit/test_marker_list.cpp` -- find_nearest edge cases
- `tests/unit/test_region_list.cpp` -- overlapping region queries
- `tests/unit/test_snap.cpp` -- snap with SmpteConverter, freeform mode edge cases
- `tests/integration/test_c_api_smf.cpp` (new) -- full C API SMF import/export round-trip
- `tests/integration/test_c_api_markers.cpp` (new) -- C API markers/regions/anchors
- `tests/CMakeLists.txt` -- add any new integration test files
- `CHANGELOG.md`

Target: every `OMEGA_ERR_*` return path exercised, every state transition tested, gcovr >= 80%.

### CHANGELOG

```markdown
### Changed
- **Sprint 6.1 -- coverage to 80%**: added unit and integration tests covering error paths, edge cases, and previously untested branches
```

### Commit & PR

```bash
git checkout -b feat/m6.1-coverage
git commit -m "test: add coverage tests to reach 80% line coverage (sprint 6.1)"
git push -u origin feat/m6.1-coverage
gh pr create --title "test: coverage to 80% (M6.1)" --body "..."
```

---

## PR 9 / Sprints 6.2-6.5 -- Sanitizer Clean, ABI Dump, API Docs, Benchmarks

**Branch:** `feat/m6-polish`

### Files to create

- `tests/benchmarks/bench_process_cycle.cpp` -- `engine.process()` with 1 track, 0 due events
- `tests/benchmarks/bench_command_enqueue.cpp` -- `SpscQueue::push()` throughput
- `tests/benchmarks/bench_tempo_lookup.cpp` -- `ns_to_ticks()` with 16-point map
- `tests/benchmarks/bench_dispatch_1k.cpp` -- `process()` with 1000 events in one cycle
- `abi/v0.6.0.dump` -- ABI baseline (remove `abi/PLACEHOLDER`)

### Files to modify

- `include/omega/omega.h` -- add doc comments (Thread/Returns/Errors) to every public function
- `README.md` -- add quick-start example (create engine, add track, add note, play, verify with CapturingSink)
- `docs/ARCHITECTURE.md` -- update to reflect M5/M5.5 additions
- `docs/STATUS.md` -- update to "M6 -- Polish (beta)"
- `docs/ROADMAP.md` -- mark M5, M5.5, M6 as done
- `tests/benchmarks/CMakeLists.txt` -- wire up benchmark sources (scaffold may already exist)
- `CHANGELOG.md`

### ABI dump

Since omega_core is STATIC, use `abi-dumper` on the static archive:
```bash
cmake -B build_abi -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOMEGA_BUILD_TESTS=OFF
cmake --build build_abi
abi-dumper build_abi/libomega_core.a -o abi/v0.6.0.dump -lver 0.6.0
rm abi/PLACEHOLDER
```

### Sanitizer verification

Verify locally -- these should already pass if earlier PRs were clean:
```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev    # ASan/UBSan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan  # TSan
```

### Documentation sweep

Go through every function in `omega.h`. Each must have:
- One-line description
- `Thread:` annotation (Mutation/Timing/Any)
- `Returns:` (for non-void)
- `Errors:` (for omega_status_t, listing every possible code)

Verify: no `TODO` or `FIXME` in public headers.

### README quick-start

The example must compile and run without modification. Verify:
```bash
cd cmake/smoke_test && cmake -B build -DCMAKE_PREFIX_PATH=../../build/install && cmake --build build && ./build/smoke_test
```

### Benchmarks

Each benchmark uses Catch2's `BENCHMARK` macro. Targets:
- `bench.process_cycle` < 5 us on a modern desktop (informational, not a hard gate)
- `bench.command_enqueue` -- throughput in ops/sec
- `bench.tempo_lookup` -- ns/lookup with 16-point map
- `bench.dispatch_1k` -- us/cycle with 1000 events

### Post-merge: Tag v0.6.0-beta

```bash
git checkout main && git pull
git tag -a v0.6.0-beta -m "M6 complete -- beta release candidate"
git push origin v0.6.0-beta
```

Create GitHub Release with CHANGELOG section.

### Commit & PR

```bash
git checkout -b feat/m6-polish
git commit -m "feat: beta polish -- ABI dump, API docs, benchmarks, sanitizer verification (sprints 6.2-6.5)"
git push -u origin feat/m6-polish
gh pr create --title "feat: beta polish (M6.2-6.5)" --body "..."
```

---

## PR 10 / v1.0.0 Stable Release

**Branch:** `chore/release-v1.0.0`

### Files to modify

- `CMakeLists.txt` -- `VERSION 0.6.0` -> `VERSION 1.0.0`, `OMEGA_SOVERSION 0` -> `OMEGA_SOVERSION 1`
- `include/omega/omega.h` -- verify version constants if macro-defined
- `CHANGELOG.md` -- promote `[Unreleased]` to `[1.0.0] - YYYY-MM-DD`, update comparison links
- `docs/STATUS.md` -- "v1.0.0 -- Stable Release"
- `docs/ROADMAP.md` -- mark all milestones done
- `docs/branching.md` -- mark all version rows as done
- `docs/migration/v0-to-v1.md` -- finalize (no breaking changes since ABI starts fresh)
- `abi/v1.0.0.dump` -- generate and commit v1.0.0 ABI baseline

### Release checklist (from docs/release-checklist.md)

- [ ] All CI jobs green on the release commit
- [ ] ABI compliance check passed
- [ ] `THIRD_PARTY_LICENSES.md` up to date
- [ ] `CHANGELOG.md` promoted to `[1.0.0]`
- [ ] `CMakeLists.txt VERSION` = `1.0.0`
- [ ] `OMEGA_SOVERSION` = `1`
- [ ] Install smoke test passing locally
- [ ] >= 80% line coverage
- [ ] Valgrind clean, ASan/TSan clean
- [ ] README quick-start compiles and runs
- [ ] No TODO/FIXME in public headers
- [ ] All design docs updated with implementation version

### Post-merge: Tag v1.0.0

```bash
git checkout main && git pull
git tag -a v1.0.0 -m "Omega v1.0.0 -- stable release"
git push origin v1.0.0
```

Create GitHub Release with CHANGELOG `[1.0.0]` section as body. Verify CI on the tag is fully green. Update Codecov badge in README.

### Commit & PR

```bash
git checkout -b chore/release-v1.0.0
git commit -m "chore: release v1.0.0"
git push -u origin chore/release-v1.0.0
gh pr create --title "chore: release v1.0.0" --body "..."
```

---

## Dependency Graph

```
PR 1 (MIDI I/O)
  |
PR 2 (SMF import + markers/regions)
  |
PR 3 (SMF export)
  |
PR 4 (OmegaTimer)  -->  TAG v0.5.0-alpha
  |
PR 5 (bump 0.6.0)
  |
PR 6 (anchors)
  |
PR 7 (snap framework)
  |
PR 8 (coverage 80%)
  |
PR 9 (polish)  -->  TAG v0.6.0-beta
  |
PR 10 (v1.0.0)  -->  TAG v1.0.0
```

All PRs are strictly serial. Each PR depends on the previous one being merged. PRs 6-7 (M5.5) technically only depend on PR 2 for markers/regions, but keeping them serial avoids merge conflicts in `omega.h` and `omega_c.cpp`.

---

## Verification

After each PR merge, the full CI matrix runs on main. After each tag, CI runs on the tag commit. Final verification for v1.0.0:

```bash
# Full local verification
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
cmake --preset release && cmake --build --preset release && ctest --preset release
bash scripts/local-ci.sh
```

Post-v1.0.0, the warp map feature from design doc 14 can be added as an additive v1.x feature without breaking ABI.
