# Contributing to Omega

Thank you for your interest in contributing. Omega is in early design/pre-implementation phase. Contributions to design documents, architectural feedback, and eventually code are all welcome.

---

## How to Contribute

### Reporting Issues

Use GitHub Issues. Please search existing issues before filing a new one.

For **bugs**: use the bug report template. Include your OS, compiler version, and a minimal reproduction case.

For **feature requests**: use the feature request template. Describe the use case, not just the feature.

For **design feedback**: open a Discussion (GitHub Discussions tab) or comment on the relevant design document. Design is still open — thoughtful critique is welcome.

---

## Development Setup

Requirements:
- C++17-capable compiler (GCC 10+, Clang 14+, MSVC 19.30 / VS 2022+, Apple Clang 14+)
- CMake 3.16+
- Git
- Linux: `libasound2-dev` (ALSA for MIDI I/O)

### Configure and build (preset-based — recommended)

```bash
git clone https://github.com/braveness23/omega.git
cd omega

# Development build with ASan + UBSan
cmake --preset dev
cmake --build --preset dev
```

### Run the test suite

```bash
ctest --preset dev
```

### Run with ThreadSanitizer

ASan and TSan are mutually exclusive; use the dedicated `tsan` preset:

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

### Manual configure (without presets)

```bash
cmake -B build -DOMEGA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Optional: Ableton Link support (changes combined license to GPL v2+):

```bash
cmake -B build -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_LINK=ON
```

---

## Code Style

Style is enforced by `.clang-format` and `.clang-tidy`. Key points:

- Indentation: 4 spaces, no tabs
- Braces: Allman style (opening brace on its own line)
- Line length: 100 characters
- Naming:
  - Types: `PascalCase`
  - Functions and variables: `snake_case`
  - Private member variables: trailing underscore (`value_`)
  - Constants and enums: `SCREAMING_SNAKE_CASE`
  - C API: `omega_` prefix for all public symbols

### Run clang-format before committing

```bash
# Check
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-18 --dry-run --Werror

# Apply
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-18 -i
```

Or install pre-commit for automatic local checks:

```bash
pip install pre-commit
pre-commit install
```

Comments only where the *why* is non-obvious. No function-header docblocks that restate the function name.

---

## Branching

All changes land on `main` via pull request — no direct pushes. Branch naming follows the
commit type prefix: `feat/<scope>`, `fix/<scope>`, `ci/<scope>`, `docs/<scope>`,
`chore/<scope>`. One concern per branch; split mixed branches before opening a PR.

See **[docs/branching.md](docs/branching.md)** for the full branching model and version
lifecycle, including the milestone → tag mapping and SOVERSION policy.

---

## Pull Request Checklist

Before opening a PR:

- [ ] `cmake --preset dev && cmake --build --preset dev` succeeds
- [ ] `ctest --preset dev` passes
- [ ] `clang-format-18` reports no changes (or `pre-commit` passes)
- [ ] `CHANGELOG.md` updated with an entry under `[Unreleased]`
  - Skip this for PRs whose title starts with `ci:`, `chore:`, or `docs:`
- [ ] PR has one concern: a bug fix, a single feature, or a documentation update

---

## Commit Message Format

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add OscSink implementation
fix: correct note-off timing when loop wraps
docs: clarify thread safety requirements in C API
test: add pattern state machine edge case tests
chore: update libremidi to v5.2.0
ci: add TSan job to workflow
```

---

## Deprecation Process

1. **Mark** the symbol with `OMEGA_DEPRECATED("use X() — remove after vY.0.0")` at its declaration in the header. Include the target removal version so it is grep-able at release time.
2. **Log** the deprecation in `CHANGELOG.md` under `### Deprecated`.
3. **Wait** at least one full MAJOR version cycle before removing. A symbol deprecated in `v1.x` is removed no earlier than `v2.0.0`.
4. **Remove** the symbol in a PR that bumps `MAJOR`, updates `OMEGA_SOVERSION`, and adds a `CHANGELOG.md` entry under `### Removed`. Reference the migration guide (`docs/migration/`).

Removing a deprecated symbol without following this process is a PR blocker.

---

## Design Documents

Design documents live in `docs/design/`. They record decisions and their rationale.

To propose a design change:
1. Open a GitHub Discussion or Issue with your proposal.
2. If consensus is reached, update the relevant design document in a PR.
3. Design documents record *decisions*, not options. Once merged, the document reflects the decision, not the debate.

---

## What We're Looking For

In the current phase (pre-implementation):
- Corrections or improvements to design documents
- Identification of missing edge cases
- Prior art we haven't considered
- Feedback from people who have built similar systems

In the implementation phase:
- Implementations of designed components
- Platform-specific sink and clock implementations
- Test coverage improvements
- Performance analysis

---

## Code of Conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). By participating you agree to its terms.
