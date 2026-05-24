#include <omega/snap.h>

#include <cstdint>
#include <limits>

namespace omega
{

namespace
{

constexpr uint64_t tick_distance(uint64_t a, uint64_t b) noexcept
{
    return a >= b ? a - b : b - a;
}

// Smallest multiple of subdiv that is >= v.
constexpr uint64_t next_multiple(uint64_t v, uint64_t subdiv) noexcept
{
    uint64_t rem = v % subdiv;
    return rem == 0u ? v : v - rem + subdiv;
}

// Largest multiple of subdiv that is <= v.
constexpr uint64_t prev_multiple(uint64_t v, uint64_t subdiv) noexcept
{
    return (v / subdiv) * subdiv;
}

}  // namespace

SnapResult snap_to_nearest(uint64_t tick,
                           const SnapConfig& config,
                           PositionConverter& converter,
                           const MarkerList& markers,
                           const RegionList& regions,
                           const AnchorList* external_anchors)
{
    uint64_t best_tick = tick;
    uint64_t best_dist = std::numeric_limits<uint64_t>::max();
    SnapTarget best_source = SnapTarget::GRID;
    bool any_candidate = false;

    auto consider = [&](uint64_t candidate_tick, SnapTarget source) {
        uint64_t dist = tick_distance(candidate_tick, tick);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_tick = candidate_tick;
            best_source = source;
            any_candidate = true;
        }
    };

    // GRID target
    if ((config.targets & SNAP_GRID) != 0u)
    {
        if (config.grid_subdiv_ticks > 0u)
        {
            uint64_t subdiv = config.grid_subdiv_ticks;
            consider(prev_multiple(tick, subdiv), SnapTarget::GRID);
            consider(next_multiple(tick, subdiv), SnapTarget::GRID);
        }
        else
        {
            // Use PositionConverter (meter-based or SMPTE-based grid).
            uint64_t next_bd = 0;
            if (converter.next_boundary(tick, next_bd) == OMEGA_OK)
            {
                consider(next_bd, SnapTarget::GRID);
            }
            uint64_t quantized = 0;
            if (converter.quantize(tick, quantized) == OMEGA_OK)
            {
                consider(quantized, SnapTarget::GRID);
            }
        }
    }

    // MARKERS target
    if ((config.targets & SNAP_MARKERS) != 0u)
    {
        for (uint32_t i = 0; i < markers.size(); ++i)
        {
            const Marker* m = markers.at(i);
            if (m != nullptr)
            {
                consider(m->tick, SnapTarget::MARKERS);
            }
        }
    }

    // REGIONS target: both start and end ticks are candidates.
    if ((config.targets & SNAP_REGIONS) != 0u)
    {
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const Region* r = regions.at(i);
            if (r != nullptr)
            {
                consider(r->start_tick, SnapTarget::REGIONS);
                consider(r->end_tick, SnapTarget::REGIONS);
            }
        }
    }

    // ANCHORS target: anchor-aware grid snap.
    // For each SNAP anchor: snapped = next grid point >= (tick + anchor.offset_ticks),
    // candidate = snapped - anchor.offset_ticks.
    if ((config.targets & SNAP_ANCHORS) != 0u && external_anchors != nullptr)
    {
        for (uint32_t i = 0; i < external_anchors->size(); ++i)
        {
            const AnchorPoint* a = external_anchors->at(i);
            if (a == nullptr || (a->flags & ANCHOR_SNAP) == 0u)
            {
                continue;
            }
            uint64_t adjusted = tick + a->offset_ticks;
            uint64_t snapped_adjusted = 0;
            if (config.grid_subdiv_ticks > 0u)
            {
                snapped_adjusted = next_multiple(adjusted, config.grid_subdiv_ticks);
            }
            else
            {
                if (converter.next_boundary(adjusted, snapped_adjusted) != OMEGA_OK)
                {
                    continue;
                }
            }
            if (snapped_adjusted >= a->offset_ticks)
            {
                consider(snapped_adjusted - a->offset_ticks, SnapTarget::ANCHORS);
            }
        }
    }

    if (!any_candidate)
    {
        return {tick, SnapTarget::GRID, false};
    }

    if (config.tolerance_ticks > 0u && best_dist > config.tolerance_ticks)
    {
        return {tick, best_source, false};
    }

    return {best_tick, best_source, true};
}

}  // namespace omega
