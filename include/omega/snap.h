#pragma once

#include <omega/anchor_point.h>
#include <omega/marker_list.h>
#include <omega/region_list.h>
#include <omega/time_signature_map.h>

#include <cstdint>

namespace omega
{

// Bitfield constants for SnapConfig::targets.
constexpr uint8_t SNAP_GRID = 0x01u;
constexpr uint8_t SNAP_MARKERS = 0x02u;
constexpr uint8_t SNAP_REGIONS = 0x04u;
constexpr uint8_t SNAP_ANCHORS = 0x08u;

// Which type of candidate won the snap.
enum class SnapTarget : uint8_t
{
    GRID = 0x01,
    MARKERS = 0x02,
    REGIONS = 0x04,
    ANCHORS = 0x08,
};

struct SnapConfig
{
    uint8_t targets;             // bitfield: SNAP_GRID | SNAP_MARKERS | SNAP_REGIONS | SNAP_ANCHORS
    uint64_t grid_subdiv_ticks;  // subdivision for grid snap (0 = use converter's default beat)
    uint64_t tolerance_ticks;    // max snap distance; 0 = unlimited
};

struct SnapResult
{
    uint64_t snapped_tick;
    SnapTarget source;
    bool did_snap;
};

/*
 * Snap tick to the nearest candidate from the enabled target sets.
 *
 * Algorithm (from design doc 14):
 *   For each enabled target, collect grid/marker/region/anchor ticks.
 *   Pick the candidate with minimum |candidate - tick|.
 *   If tolerance_ticks > 0 and distance exceeds it, return did_snap=false.
 *
 * When SNAP_GRID is set and converter.next_boundary() returns OMEGA_ERR_NO_METER,
 * the grid target is skipped (freeform mode -- no metric grid available).
 *
 * The external_anchors parameter is an optional AnchorList for per-item snap
 * anchors. Pass nullptr to skip ANCHORS even if the flag is set.
 *
 * Thread: Not thread-safe. Must not be called from the timing thread.
 */
SnapResult snap_to_nearest(uint64_t tick,
                           const SnapConfig& config,
                           PositionConverter& converter,
                           const MarkerList& markers,
                           const RegionList& regions,
                           const AnchorList* external_anchors);

}  // namespace omega
