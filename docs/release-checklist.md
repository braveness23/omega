# Release Checklist

Copy this checklist for each release. Replace `X.Y.Z` with the version being released.

---

## Release Checklist — vX.Y.Z

### Pre-release

- [ ] All CI jobs green on the release commit
- [ ] ABI compliance check passed (if `include/` or `src/` changed)
- [ ] `THIRD_PARTY_LICENSES.md` up to date with all pinned versions
- [ ] `CHANGELOG.md` `[Unreleased]` section renamed to `[X.Y.Z]` with today's date
- [ ] `CMakeLists.txt` `VERSION` updated to `X.Y.Z`
- [ ] `OMEGA_SOVERSION` updated (if ABI changed)
- [ ] Install smoke test passing locally

### Tagging

- [ ] `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
- [ ] `git push origin vX.Y.Z`

### Post-release

- [ ] GitHub Release created with the `[X.Y.Z]` CHANGELOG section as the release body
- [ ] New ABI dump committed to `abi/` (baseline for the next release's `abidiff` check):
      `abi-dumper libomega.so -o abi/omega-X.Y.Z.dump -lver X.Y.Z`
- [ ] New empty `[Unreleased]` section added to the top of `CHANGELOG.md`

---

## Versioning Policy Summary

| Change type | Version bump |
|---|---|
| Bug fix, no API change | PATCH |
| New public API, old API unchanged | MINOR |
| Any API removal or behavior change observable by consumers | MAJOR |
| Any ABI break (struct layout, symbol signature, vtable) | MAJOR |
| Deprecation added | MINOR |
| Deprecated symbol removed | MAJOR |

Before `v1.0.0`, MINOR bumps may include breaking changes with a documented
migration path. After `v1.0.0`, they never do.

See `docs/design/12-library-foundation.md §2` for the full stability contract.
