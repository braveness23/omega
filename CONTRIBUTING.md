# Contributing to Omega

Thank you for your interest in contributing. Omega is in early design/pre-implementation phase. Contributions to design documents, architectural feedback, and eventually code are all welcome.

---

## How to Contribute

### Reporting Issues

Use GitHub Issues. Please search existing issues before filing a new one.

For **bugs**: use the bug report template. Include your OS, compiler version, and a minimal reproduction case.

For **feature requests**: use the feature request template. Describe the use case, not just the feature.

For **design feedback**: open a Discussion (GitHub Discussions tab) or comment on the relevant design document. Design is still open — thoughtful critique is welcome.

### Pull Requests

1. **Fork** the repository and create a branch from `main`.
2. **One concern per PR**: a bug fix, a single feature, or a design document update. Not all three.
3. **Tests required**: new functionality requires tests. The test suite must pass before review begins.
4. **Code style**: run `clang-format` before committing. The `.clang-format` file is in the repository root.
5. **Commit messages**: use the [Conventional Commits](https://www.conventionalcommits.org/) format:
   - `feat: add OscSink implementation`
   - `fix: correct note-off timing when loop wraps`
   - `docs: clarify thread safety requirements in C API`
   - `test: add pattern state machine edge case tests`
   - `chore: update libremidi to 5.x`
6. **DCO**: By submitting a PR you certify that you have the right to contribute the code under the MIT license. See the [Developer Certificate of Origin](https://developercertificate.org/).

### Design Documents

Design documents live in `docs/design/`. They record decisions and their rationale. To propose a design change:

1. Open a GitHub Discussion or Issue with your proposal.
2. If consensus is reached, update the relevant design document in a PR.
3. Design documents record *decisions*, not options. Once a decision is made and merged, the document should reflect the decision, not the debate.

---

## Development Setup

Requirements:
- C++17-capable compiler (GCC 10+, Clang 11+, MSVC 2019+)
- CMake 3.16+
- Git

```bash
git clone https://github.com/braveness23/omega.git
cd omega
cmake -B build -DOMEGA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Optional (Ableton Link support, changes license to GPL v2+):
```bash
cmake -B build -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_LINK=ON
```

---

## Code Style

Omega follows a consistent style enforced by `.clang-format`. Key points:

- Indentation: 4 spaces, no tabs
- Braces: Allman style (opening brace on its own line)
- Line length: 100 characters
- Naming:
  - Types: `PascalCase`
  - Functions and variables: `snake_case`
  - Private member variables: trailing underscore (`value_`)
  - Constants and enums: `SCREAMING_SNAKE_CASE`
  - C API: `omega_` prefix for all public symbols

Comments only where the *why* is non-obvious. No function-header docblocks that restate the function name.

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
