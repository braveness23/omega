# Migration Guide: v0.x → v1.0

**Status:** Template — fill in when v1.0.0 is ready to release.

This document describes breaking changes between the v0.x series and v1.0.0,
and how to update consuming code for each change.

---

## What v1.0.0 Means

As of v1.0.0, the C API (`omega.h`) is ABI-stable within a MAJOR version.
Binaries compiled against v1.0.0 are binary-compatible with any v1.x.y release
without recompilation.

The C++ internal API (`omega::core`) carries no ABI stability guarantee between
any versions and is not subject to this migration guide.

---

## Breaking Changes

<!-- Document each API or behavioral change here. Format:

### omega_foo_bar() removed

**Why:** <reason>

**Before:**
```c
omega_foo_bar(engine, ...);
```

**After:**
```c
omega_baz_qux(engine, ...);
```
-->

*(No breaking changes recorded yet — fill in before v1.0.0 release.)*

---

## Deprecated Symbols Removed

<!-- List symbols that were deprecated in v0.x and removed in v1.0.0. -->

*(None yet.)*

---

## New in v1.0.0

<!-- High-level summary of new capabilities; link to CHANGELOG for details. -->

*(Fill in before v1.0.0 release.)*
