# 14 — Markers, Regions, and Anchor Points

## Status

Design discussion — pre-M5. Not yet implemented.

---

## Markers and Regions

**Markers** are named tick positions stored in `Session` — the same kind of session-level coordinate data as `TempoMap` and `TimeSignatureMap`. A `MarkerList` in `Session` is a natural fit and gives snap utilities a concrete target to snap to.

**Regions** are tick spans `{ name, start_tick, end_tick, type }`. A generic region type unifies loop regions, punch-in ranges, and section labels. The `SongArrangement` is a specialized region concept; a general `Region` would sit alongside it in `Session`.

**Enforcement split**: the library stores marker and region data. Transport enforcement (auto-wrap at loop end, punch-in/out) is the application's responsibility. The engine stays simple; the app controls transport by sending locate commands.

---

## AnchorPoint

An `AnchorPoint` is a named tick offset within an item (pattern or event) that serves multiple roles:

```cpp
struct AnchorPoint {
    std::string  name;
    uint64_t     offset_ticks;
    uint32_t     flags;
};
```

### Flags (combinable)

| Flag | Meaning |
|---|---|
| `ANCHOR_SNAP` | Eligible as a snap anchor |
| `ANCHOR_WARP` | Warp map pivot point |
| `ANCHOR_CUE`  | Named cue / editorial label |

### Scope

- **Patterns** — anchor list stored directly on the pattern definition. Intrinsic anchors (e.g. "downbeat is 40 ticks in due to pickup") travel with the pattern; per-placement overrides are app-managed.
- **Events** — anchor list stored in a **side table** keyed by event ID (sparse, same pattern as the blob store for sysex/OSC). Events stay 24 bytes; anchored events are the exception, not the rule.

### Active snap anchor

Each item has one designated active snap anchor used by the snap framework. Defaults to offset 0 (item start). Can be set to any `ANCHOR_SNAP`-flagged point, or disabled entirely. Selection policy (nearest to cursor, explicit, disabled) is app-configured — the library provides the mechanism, the application owns the policy.

---

## Warp Map

Warp points are `AnchorPoint` entries flagged `ANCHOR_WARP`. Omega's responsibility is to **produce the warp map** — an ordered list of `(offset_ticks, target_ticks)` pairs that describe how content time maps to timeline time. The **application owns stretching** (audio time-stretch, MIDI tick rescaling between pivots).

This is a shared-responsibility split consistent with Omega's overall design philosophy: the engine provides data and coordinates; platform integrations do I/O and processing.

---

## Snap Framework

The snap calculation with anchor points:

```
snap_target  = nearest_grid_point(item_start + active_anchor.offset_ticks)
item_start   = snap_target - active_anchor.offset_ticks
```

Snap targets include: tempo grid, beat/bar grid (via `MeterCursor`), marker positions, region boundaries, and other items' anchor points. The framework accepts `PositionConverter&` and is agnostic to which coordinate system is active.

---

## Implementation Timeline

| Phase | What | Rationale |
|---|---|---|
| **M5** | `MarkerList` in `Session`; SMF marker meta-event round-trip (FF 06/07) on import/export | Small; if skipped, SMF code has to be reopened later |
| **M5.5** | `AnchorPoint` type, anchor lists on patterns, event side table, snap framework C API | Sprint-sized; doesn't block M5 or M6 polish |
| **M6 / v1.0.0** | Snap framework included in v1.0 surface | ABI-stability argument: app authors building editorial UIs need the data types stable |
| **Post-v1.0.0** | Warp map production | Most complex piece; nothing in the planned architecture blocks adding it as an additive v1.x feature |

The main risk of deferring anchors past v1.0.0 is that v1.0.0 is the first ABI-stable release. Additions after that are fine (additive), but applications shipping against v1.0 won't have anchor support. Getting the data types and C API into v1.0 — even without the warp map — is the right call.

---

## Open Questions

- Named anchor presets on patterns (e.g. a drum loop ships with a "downbeat" anchor by convention)?
- Should `MarkerList` and `Region` list live directly on `Session` or be optional session attachments?
- C API surface for anchor CRUD — deferred until implementation milestone.
