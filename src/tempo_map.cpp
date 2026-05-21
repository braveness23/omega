#include <omega/tempo_map.h>
#include <omega/types.h>

#include <algorithm>

namespace omega
{

// nanoseconds = ticks * 60_000_000_000_000 / (bpm_milli * PPQN)
// No floating-point; multiply first to preserve precision.
uint64_t TempoMap::segment_ticks_to_ns(uint64_t ticks, uint32_t bpm_milli)
{
    return (ticks * 60'000'000'000'000ULL) / (static_cast<uint64_t>(bpm_milli) * PPQN);
}

// ticks = ns * bpm_milli * PPQN / 60_000_000_000_000
uint64_t TempoMap::segment_ns_to_ticks(uint64_t ns, uint32_t bpm_milli)
{
    return (ns * static_cast<uint64_t>(bpm_milli) * PPQN) / 60'000'000'000'000ULL;
}

TempoMap::TempoMap()
{
    points_.push_back({0u, 120'000u, 0u});
}

void TempoMap::insert(uint64_t tick, uint32_t bpm_milli)
{
    // Capture ns at the insertion point before any modifications.
    uint64_t ns = ticks_to_ns(tick);

    auto it =
        std::lower_bound(points_.begin(), points_.end(), tick, [](const TempoPoint& p, uint64_t t) {
            return p.tick < t;
        });

    if (it != points_.end() && it->tick == tick)
    {
        it->bpm_milli = bpm_milli;
        it->ns_at_tick = ns;
    }
    else
    {
        it = points_.insert(it, {tick, bpm_milli, ns});
    }

    // Recompute ns_at_tick for all points that follow the inserted/updated one.
    for (auto next = std::next(it); next != points_.end(); ++next)
    {
        auto prev = std::prev(next);
        uint64_t elapsed = next->tick - prev->tick;
        next->ns_at_tick = prev->ns_at_tick + segment_ticks_to_ns(elapsed, prev->bpm_milli);
    }
}

uint64_t TempoMap::ticks_to_ns(uint64_t ticks) const
{
    // Find the last TempoPoint whose tick <= ticks.
    auto it = std::upper_bound(
        points_.begin(), points_.end(), ticks, [](uint64_t t, const TempoPoint& p) {
            return t < p.tick;
        });
    if (it != points_.begin())
        --it;

    return it->ns_at_tick + segment_ticks_to_ns(ticks - it->tick, it->bpm_milli);
}

uint64_t TempoMap::ns_to_ticks(uint64_t ns) const
{
    // Find the last TempoPoint whose ns_at_tick <= ns.
    auto it =
        std::upper_bound(points_.begin(), points_.end(), ns, [](uint64_t n, const TempoPoint& p) {
            return n < p.ns_at_tick;
        });
    if (it != points_.begin())
        --it;

    return it->tick + segment_ns_to_ticks(ns - it->ns_at_tick, it->bpm_milli);
}

}  // namespace omega
