# Design: Library Foundation

**Status**: Proposed.

This document defines the non-implementation infrastructure that a permanent library requires before any code is written. Every item here is a gate, not a suggestion. A library missing any of these will accumulate technical debt that compounds with every release.

Three items — the ABI baseline dump, the `abidiff` CI check, and the benchmark implementations — cannot be populated until compiled code exists. Their scaffolding (the CI jobs, the directory structure, the placeholders) is established now; the content arrives with the first compiled library. All other items are completed before the first implementation commit.

---

## Domain 1: Build System for Consumers

A library that is hard to consume will not be consumed.

### 1.1 Symbol Visibility

All shared library builds must default to hidden visibility. Public symbols are explicitly annotated.

```cmake
set_target_properties(omega_core PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
)
```

A generated export header provides the `OMEGA_API` macro:

```cmake
include(GenerateExportHeader)
generate_export_header(omega_core
    BASE_NAME         OMEGA
    EXPORT_FILE_NAME  include/omega/export.h
)
```

Every public C++ symbol is annotated `OMEGA_API`. The C API (`omega.h`) uses the same macro. Without this, the shared library leaks internal symbols and ABI stability guarantees are meaningless.

### 1.2 CMakePresets.json

A `CMakePresets.json` in the repo root defines canonical configure, build, and test presets. All CI jobs and contributors use these identically. No tribal knowledge about which flags go where.

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev",
      "displayName": "Development (tests + ASan/UBSan)",
      "binaryDir": "${sourceDir}/build/dev",
      "cacheVariables": {
        "OMEGA_BUILD_TESTS":     "ON",
        "OMEGA_WITH_SANITIZERS": "ON",
        "CMAKE_BUILD_TYPE":      "Debug"
      }
    },
    {
      "name": "tsan",
      "displayName": "Development (tests + TSan)",
      "binaryDir": "${sourceDir}/build/tsan",
      "cacheVariables": {
        "OMEGA_BUILD_TESTS":      "ON",
        "OMEGA_WITH_TSAN":        "ON",
        "CMAKE_BUILD_TYPE":       "Debug"
      }
    },
    {
      "name": "release",
      "displayName": "Release",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": {
        "OMEGA_BUILD_TESTS":  "ON",
        "CMAKE_BUILD_TYPE":   "Release"
      }
    },
    { "name": "ci-linux",   "inherits": "release" },
    { "name": "ci-macos",   "inherits": "release" },
    { "name": "ci-windows", "inherits": "release" }
  ],
  "buildPresets": [
    { "name": "dev",     "configurePreset": "dev"     },
    { "name": "tsan",    "configurePreset": "tsan"    },
    { "name": "release", "configurePreset": "release" }
  ],
  "testPresets": [
    {
      "name": "dev",
      "configurePreset": "dev",
      "output": { "verbosity": "normal" }
    },
    {
      "name": "tsan",
      "configurePreset": "tsan",
      "output": { "verbosity": "normal" }
    }
  ]
}
```

ASan+UBSan (`dev`) and TSan (`tsan`) are separate presets because `-fsanitize=thread` is incompatible with `-fsanitize=address`.

### 1.3 OmegaConfig.cmake.in — Complete Content

The existing `cmake/OmegaConfig.cmake.in` must declare all transitive dependencies so that downstream `find_package(omega)` works without any manual wiring.

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# FetchContent is a build-time concept; installed consumers use find_dependency.
find_dependency(libremidi REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/OmegaTargets.cmake")

check_required_components(omega)
```

If libremidi ships no CMake config file, provide `cmake/FindLibremidi.cmake` and install it alongside `OmegaTargets.cmake`.

### 1.4 pkg-config

Not every consumer uses CMake. `pkg-config` is required for Linux packaging and for consumers using Meson, Autotools, or manual build systems.

```
# cmake/omega.pc.in
prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@

Name: omega
Description: @PROJECT_DESCRIPTION@
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lomega
Cflags: -I${includedir}
```

```cmake
configure_file(cmake/omega.pc.in ${CMAKE_CURRENT_BINARY_DIR}/omega.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/omega.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
```

---

## Domain 2: Versioning and Stability Contract

A library without an explicit stability contract cannot be depended on.

### 2.1 Semantic Versioning Policy

| Change type | Version bump |
|---|---|
| Bug fix, no API change | PATCH |
| New public API, old API unchanged | MINOR |
| Any API removal or behavior change observable by consumers | MAJOR |
| Any ABI break (struct layout, symbol signature, vtable) | MAJOR |
| Deprecation added | MINOR |
| Deprecated symbol removed | MAJOR |

Before `v1.0.0`, MINOR bumps may include breaking changes with a documented migration path. After `v1.0.0`, they never do.

### 2.2 ABI Versioning

The shared library uses an `SOVERSION` separate from the project version. The `SOVERSION` increments only on ABI-breaking changes and typically lags behind `VERSION`.

```cmake
set_target_properties(omega PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${OMEGA_SOVERSION}
)
```

`OMEGA_SOVERSION` starts at `0`. It is bumped manually as part of the release process when the ABI breaks. It is never bumped automatically.

### 2.3 ABI Compliance Checking in CI

The CI pipeline runs `abidiff` (libabigail) on every PR that touches `include/` or `src/`. The check compares the built shared library against the last tagged release's ABI dump stored in `abi/`.

```bash
# Generate a dump at release time — committed to abi/
abi-dumper libomega.so -o abi/omega-0.1.0.dump -lver 0.1.0

# Check on each qualifying PR
abidiff abi/omega-<last-tag>.dump <current-build>/libomega.so
```

Any unintentional ABI break fails the PR. Intentional ABI breaks require updating `OMEGA_SOVERSION` and a CHANGELOG entry.

**Stub**: The CI job is created now. The `abi/` directory contains a `PLACEHOLDER` file noting that the baseline dump is generated at `v0.1.0`. The job is a no-op until the first real dump exists.

### 2.4 Deprecation Policy

Deprecated symbols are marked at declaration, emit a compiler warning, and survive for at minimum one MAJOR version cycle. `OMEGA_DEPRECATED` is defined in `export.h`:

```c
// Compiler-portable deprecation with message
#if defined(__GNUC__) || defined(__clang__)
#  define OMEGA_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#  define OMEGA_DEPRECATED(msg) __declspec(deprecated(msg))
#else
#  define OMEGA_DEPRECATED(msg)
#endif
```

Usage:

```cpp
// C++ — annotate at declaration, not definition
[[deprecated("use new_function() — remove after v2.0.0")]]
void old_function();

// C API
OMEGA_DEPRECATED("use omega_new_fn() — remove after v2.0.0")
omega_status_t omega_old_fn(omega_engine_t* e);
```

The target removal version is part of the annotation so it is grep-able at release time.

### 2.5 CHANGELOG Discipline

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com) format with an `[Unreleased]` section at the top. Every PR that changes public behavior, fixes a bug, or adds/removes a symbol must include a `CHANGELOG.md` entry. A CI lint step enforces this. The check is skipped for commits whose type is `ci:`, `chore:`, or `docs:` (internal-only documentation changes).

---

## Domain 3: Dependency Hygiene

### 3.1 Version Pinning

No dependency may be fetched from a branch or `master`. Every `FetchContent_Declare` uses a specific Git tag or commit hash. Floating references are a CI reliability hazard and a supply-chain risk.

```cmake
FetchContent_Declare(libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG        v5.1.0          # pinned — update deliberately via PR
    GIT_SHALLOW    TRUE
)
```

Dependencies are reviewed and bumped quarterly or when a CVE is disclosed. All bumps go through a PR so CI validates the change before it lands.

### 3.2 Dependency Inventory

`THIRD_PARTY_LICENSES.md` enumerates every dependency: version, license, source URL, and whether it is linked into the installed library. This is required by most corporate legal teams before they will take a dependency on a library.

```
## libremidi
- Version: v5.1.0
- License: MIT
- Source: https://github.com/celtera/libremidi
- Ships to consumers: yes — linked into the installed library

## midifile
- Version: <commit hash>
- License: BSD 2-Clause
- Source: https://github.com/craigsapp/midifile
- Ships to consumers: yes — linked into the installed library

## Catch2
- Version: v3.5.2
- License: BSL-1.0
- Source: https://github.com/catchorg/Catch2
- Ships to consumers: no — test framework only, never installed
```

The inventory is updated as part of the release checklist. Adding a dependency without updating `THIRD_PARTY_LICENSES.md` is a PR blocker.

---

## Domain 4: CI / Quality Gates

### 4.1 Compiler and Standard Matrix

The officially supported matrix is documented here and enforced by CI. Any compiler or version not in this matrix is best-effort.

| Compiler | Minimum version | Standards tested |
|---|---|---|
| GCC | 10 | C++17, C++20 |
| Clang | 14 | C++17, C++20 |
| MSVC | 19.30 (VS 2022) | C++17 |
| Apple Clang | 14 | C++17 |

CI tests the full matrix on every PR. C++20 jobs are informational (allowed to fail) until C++20 support is formally declared, at which point they become required gates.

### 4.2 ThreadSanitizer in CI

The two-thread model is the engine's core invariant. A dedicated CI job runs the full test suite with TSan via the `tsan` preset. This job is separate from the ASan+UBSan job; the two sanitizer families cannot be combined.

### 4.3 clang-tidy

`.clang-tidy` is checked in at the repo root with a locked-down check set. CI runs `clang-tidy` on files changed by a PR. The full build is not linted on every PR — that is too slow for feedback. The config is treated as code: changes to `.clang-tidy` require PR review.

```yaml
Checks: >
  clang-analyzer-*,
  cppcoreguidelines-*,
  performance-*,
  readability-*,
  modernize-*,
  -modernize-use-trailing-return-type,
  -cppcoreguidelines-pro-bounds-pointer-arithmetic
WarningsAsErrors: "*"
```

### 4.4 Coverage Reporting

Coverage is a visible dashboard, not a hard gate. The CI uploads an LCOV report to Codecov on every push to `main`. Coverage regressions are flagged as warnings on PRs. Hard minimums apply once implementation begins:

| Module | Minimum |
|---|---|
| Core engine | 90% |
| C API | 85% |
| Test utilities | 70% |

### 4.5 Benchmark Baseline

Canonical benchmarks live in `tests/benchmarks/` and run in CI on `main` pushes only (not on PRs — too slow). Results are stored as CI artifacts so regressions are visible over time.

Required benchmarks:
- `bench_process_cycle` — one `engine.process()` call with a fully-loaded session
- `bench_command_enqueue` — enqueue and drain 1000 commands through the SPSC queue
- `bench_tempo_lookup` — `TempoMap::ns_to_tick()` on a map with 100 tempo points

**Stub**: The `tests/benchmarks/` directory and CI job exist now. The benchmark source files are placeholders until the engine types they exercise exist.

---

## Domain 5: Release Process

### 5.1 Git Tagging Convention

Tags are `vMAJOR.MINOR.PATCH`, annotated (not lightweight). Every tag points to a commit where:
- `CHANGELOG.md` has the version header filled in (not `[Unreleased]`)
- `CMakeLists.txt` `VERSION` matches the tag
- `OMEGA_SOVERSION` is updated if the ABI changed

Tags are never force-pushed after publication.

### 5.2 Release Checklist

Stored in `docs/release-checklist.md`. The checklist is executed manually — automation enforces the gates, but human judgment determines whether the release is ready.

```
## Release Checklist — vX.Y.Z

Pre-release:
- [ ] All CI jobs green on the release commit
- [ ] ABI compliance check passed (if include/ or src/ changed)
- [ ] THIRD_PARTY_LICENSES.md up to date with all pinned versions
- [ ] CHANGELOG.md [Unreleased] renamed to [X.Y.Z] with today's date
- [ ] CMakeLists.txt VERSION updated to X.Y.Z
- [ ] OMEGA_SOVERSION updated (if ABI changed)
- [ ] Install smoke test passing

Tagging:
- [ ] git tag -a vX.Y.Z -m "Release vX.Y.Z"
- [ ] git push origin vX.Y.Z

Post-release:
- [ ] GitHub Release created with CHANGELOG section as body
- [ ] New ABI dump committed to abi/ (baseline for next release's check)
- [ ] New empty [Unreleased] section added to CHANGELOG.md
```

### 5.3 Install Smoke Test

CI validates the full install-and-consume path on every PR. The job:
1. Builds and installs Omega into a temporary prefix
2. Builds a minimal consumer project against the install via `find_package(omega REQUIRED)`
3. Verifies the consumer compiles, links, and runs

This catches broken `OmegaConfig.cmake`, missing transitive dependencies, and install path regressions before they reach a release.

### 5.4 Long-Term Support Policy

Each MAJOR version receives bug-fix support for a minimum of two years from its release date. Security fixes are backported for the same window. No new API is added to a MAJOR version after the next MAJOR is released.

---

## Domain 6: API Contract in Headers

Design documents are authoritative for design decisions. Headers are authoritative for the API contract. The two must agree, and the headers travel with the library. Application authors must be able to use the API correctly by reading the header alone.

### 6.1 Thread Safety Annotations

Every public function in `omega.h` and every public C++ method declares its thread safety at the declaration site. The allowed tags are exactly four:

- `Thread: Mutation thread only.`
- `Thread: Timing thread only.`
- `Thread: Any thread.`
- `Thread: Thread-unsafe — external lock required.`

```c
/*
 * Thread: Mutation thread only.
 * Must not be called concurrently with another mutation; serialize externally.
 */
omega_status_t omega_engine_add_source(omega_engine_t* e, omega_source_t* src);

/*
 * Thread: Any thread.
 */
omega_version_t omega_version(void);
```

### 6.2 Error Contract at Declaration

Every function returning `omega_status_t` documents the complete set of codes it can return and the conditions for each. No undocumented codes. No surprises.

```c
/*
 * Returns:
 *   OMEGA_OK            — source registered; active on next process() cycle
 *   OMEGA_ERR_NOMEM     — registry allocation failed
 *   OMEGA_ERR_INVALID   — e or src is NULL
 *   OMEGA_ERR_QUEUE_FULL — mutation queue at capacity
 */
omega_status_t omega_engine_add_source(omega_engine_t* e, omega_source_t* src);
```

### 6.3 Ownership Rules at Declaration

Every `omega_*_create()` function documents its ownership contract at the declaration:

```c
/*
 * Ownership: caller-owned. Caller must call omega_engine_destroy() before exit.
 * The engine holds non-owning references to all objects passed to it.
 * Destroy all dependent objects before destroying the engine.
 */
omega_engine_t* omega_engine_create(const omega_engine_config_t* cfg);
```

### 6.4 ABI Stability Guarantee

As of `v1.0.0`, the C API (`omega.h`) is ABI-stable within a MAJOR version. Callers compiled against `v1.0.0` may run against any `v1.x.y` without recompilation. The C++ internal API (`omega::core`) carries no ABI stability guarantee between any versions.

This guarantee is stated in `omega.h` at the version block:

```c
/*
 * ABI stability: the C API is ABI-stable within a MAJOR version (>= 1.0.0).
 * Binaries compiled against v1.0.0 are compatible with any v1.x.y.
 * The C++ omega::core API carries no ABI stability guarantee.
 */
```

---

## Domain 7: Developer Experience Baseline

### 7.1 .editorconfig

Ensures consistent whitespace before clang-format runs. Prevents `\r\n` line endings from Windows contributors silently corrupting files.

```ini
root = true

[*]
charset              = utf-8
end_of_line          = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{cpp,h,hpp}]
indent_style = space
indent_size  = 4

[CMakeLists.txt,*.cmake]
indent_style = space
indent_size  = 4

[*.md]
trim_trailing_whitespace = false
```

### 7.2 pre-commit Configuration

`.pre-commit-config.yaml` gives contributors fast local feedback before CI sees the change. Install once with `pip install pre-commit && pre-commit install`.

```yaml
repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: end-of-file-fixer
      - id: trailing-whitespace
      - id: mixed-line-ending
        args: [--fix=lf]
      - id: check-merge-conflict

  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v18.1.0
    hooks:
      - id: clang-format
        types_or: [c, c++]

  - repo: https://github.com/pocc/pre-commit-hooks
    rev: v1.3.5
    hooks:
      - id: clang-tidy
```

The CI clang-format and clang-tidy jobs are the authoritative gates; pre-commit is the fast-feedback path for contributors.

### 7.3 CONTRIBUTING.md Requirements

`CONTRIBUTING.md` must cover all of the following. Any gap is a defect in the contributor experience, not an oversight to tolerate.

- How to configure and build locally: `cmake --preset dev && cmake --build --preset dev`
- How to run the test suite: `ctest --preset dev`
- How to run sanitizers: `cmake --preset tsan && ctest --preset tsan`
- How to run clang-format locally (before committing)
- The PR checklist: CHANGELOG entry, format clean, tidy clean
- Commit message format (Conventional Commits)
- The deprecation process: how to mark, how long a deprecated symbol lives, how removal is approved

---

## Implementation Order

All items below are completed before the first implementation commit. The three stub items are scaffolded now and filled in when compiled code exists.

| Item | Deliverable | Stub? |
|---|---|---|
| Symbol visibility | `generate_export_header` + `OMEGA_API` macro | — |
| `OMEGA_DEPRECATED` macro | Defined in `export.h` | — |
| `CMakePresets.json` | dev / tsan / release / ci presets | — |
| `OMEGA_WITH_TSAN` CMake option | Separate sanitizer flag for TSan preset | — |
| Dependency pinning | Pinned tags on all `FetchContent` deps | — |
| `THIRD_PARTY_LICENSES.md` | All current deps inventoried | — |
| `.editorconfig` | Checked in | — |
| `.pre-commit-config.yaml` | Checked in | — |
| `.clang-tidy` | Baseline config checked in | — |
| `CHANGELOG.md` lint in CI | Job that fails if `[Unreleased]` untouched | — |
| Compiler matrix CI jobs | GCC / Clang / MSVC / Apple Clang matrix | — |
| TSan CI job | Separate from ASan+UBSan job | — |
| Coverage reporting | Codecov integration on `main` pushes | — |
| `cmake/omega.pc.in` | pkg-config template + install rule | — |
| `OmegaConfig.cmake.in` content | `find_dependency` calls for transitive deps | — |
| `docs/release-checklist.md` | Full checklist | — |
| Install smoke test | CI job: install → consumer project → link | — |
| Migration guide template | `docs/migration/v0-to-v1.md` | — |
| LTS policy | Documented in this doc (§5.4) | — |
| `CONTRIBUTING.md` audit | Updated to meet §7.3 requirements | — |
| Header documentation conventions | Thread safety / error / ownership templates live in `omega.h` before any functions are added | — |
| ABI baseline dump | `abi/` directory + placeholder | **stub** |
| `abidiff` CI job | Job created; no-op until baseline exists | **stub** |
| Benchmark source files | `tests/benchmarks/` + CI job; sources are placeholders | **stub** |

---

## What This Document Does Not Cover

- **Code-level testing standards** — design 08
- **C API surface design** — design 04
- **Thread model** — design 02
- **Memory model** — design 03

This document concerns only the infrastructure that makes those designs shippable as a permanent library.
