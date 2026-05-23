# Branching Strategy and Version Lifecycle

---

## Branching Model

Omega uses **GitHub Flow**: `main` is the only long-lived branch. All work — features, fixes,
CI changes, documentation — goes through a short-lived branch and a pull request. There is no
`develop` branch and no permanent `release/*` branches before v1.0.0.

### Branch naming

| Prefix | Use case | Commit type |
|---|---|---|
| `feat/<scope>` | New capability | `feat:` |
| `fix/<scope>` | Bug fix | `fix:` |
| `ci/<scope>` | CI/CD changes only | `ci:` |
| `docs/<scope>` | Documentation only | `docs:` |
| `chore/<scope>` | Maintenance, dep bumps | `chore:` |

Examples: `feat/m2-clock-source`, `fix/windows-path-mangling`, `ci/dep-caching`.

One concern per branch. Split a branch that fixes an unrelated bug alongside a feature before
opening the PR.

### `main` branch rules

- No direct pushes. All changes land via PR.
- All non-informational CI jobs must pass before merge.
- Commits on `main` follow [Conventional Commits](https://www.conventionalcommits.org/).

### Release branches — v1.0.0 and later

Not used before v1.0.0. Alpha and beta milestones are tagged directly from `main`; pre-release
versions are not patched after tagging — development continues on `main`.

At v1.0.0, introduce `release/1.x`:

1. Cut `release/1.x` from `main` when the release is ready.
2. Stabilise, bump `CMakeLists.txt VERSION`, tag `v1.0.0` on `release/1.x`.
3. Fast-forward merge back to `main`.
4. Hotfixes: branch `hotfix/1.0.1` from the tag, merge to both `release/1.x` and `main`.

---

## Version Lifecycle

### Milestone → tag mapping

| Milestone | `CMakeLists.txt VERSION` during dev | Tag on completion |
|---|---|---|
| M1 done | `0.1.0` → tag → bump to `0.2.0` | `v0.1.0` ✓ |
| M2 done | `0.2.0` → tag → bump to `0.3.0` | `v0.2.0` ✓ |
| M3 done | `0.3.0` → tag → bump to `0.4.0` | `v0.3.0` ✓ |
| M4+M4.5 done | `0.4.0` → tag → bump to `0.5.0` | `v0.4.0` ✓ |
| M5 done | `0.5.0` → tag → bump to `0.6.0` | `v0.5.0-alpha` |
| M6 done | `0.6.0` → tag → bump to `1.0.0` | `v0.6.0-beta` |
| Stable release | `1.0.0` | `v1.0.0` |

`CMakeLists.txt VERSION` always tracks the **next intended release**. It matches the tag at the
moment of tagging, then is bumped immediately after.

### Pre-release qualifiers

`-alpha` and `-beta` appear in **git tags only**. They do not appear in:

- `CMakeLists.txt VERSION` — cmake's VERSION field does not support pre-release identifiers.
- `omega_version()` — returns `{major, minor, patch}` with no pre-release flag.
- `pkg-config Version:` — same.

Pre-release status is signalled by the tag name and a README status badge. This matches the
convention used by libremidi, Catch2, and most C++ libraries.

### SOVERSION

`OMEGA_SOVERSION` stays at `0` through all alpha and beta tags. It bumps to `1` at v1.0.0.
After v1.0.0 it only bumps on ABI-breaking changes, per the policy in
`docs/release-checklist.md`.

### Tag format and procedure

Tags are always **annotated** so the message appears in `git describe` and GitHub Releases:

```bash
git tag -a v0.5.0-alpha -m "M5 complete — real MIDI I/O and SMF import/export"
git push origin v0.5.0-alpha
```

The CI workflow fires on `v*` tags, so the full build matrix runs on the exact tagged commit
before a GitHub Release is created. Follow `docs/release-checklist.md` for the full checklist.

### Patch versions during alpha/beta

`v0.1.1-alpha` etc. are reserved for critical bug fixes reported against a shipped alpha.
The preference is to fix forward on `main` and advance to the next milestone tag rather than
patch an alpha.

---

## How the CI interacts with branches and tags

| Event | Jobs that run |
|---|---|
| PR to `main` (code changed) | All jobs including `clang-tidy`, `changelog-lint`, `abi-check` |
| PR to `main` (docs only) | `format-check`, `changelog-lint` (build jobs skipped by path filter) |
| Push to `main` | Full build matrix + `coverage` + `benchmarks` |
| Push of `v*` tag | Full build matrix (no `clang-tidy` / `changelog-lint` — those need a PR) |
