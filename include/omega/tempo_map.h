#pragma once

#include <cstdint>
#include <vector>

namespace omega
{

/*
 * Maps between wall-clock nanoseconds and musical ticks across tempo changes.
 *
 * Default state: single point at tick=0, bpm_milli=120000 (120 BPM).
 * All arithmetic is integer-only; no floating-point in any hot-path method.
 *
 * Thread: instances are not thread-safe. All accesses must be from one thread
 * at a time, or externally synchronized.
 */
class TempoMap
{
public:
    struct TempoPoint
    {
        uint64_t tick;
        uint32_t bpm_milli;
        uint64_t ns_at_tick; /* precomputed wall-clock ns at this tick */
    };

    TempoMap();

    /*
     * Insert a tempo change at the given tick position.
     * Recomputes ns_at_tick for all subsequent points.
     * If a point already exists at tick, it is replaced.
     */
    void insert(uint64_t tick, uint32_t bpm_milli);

    /*
     * Remove the tempo point at exactly tick.
     * No-op if no point exists at tick, or if tick == 0
     * (the origin point cannot be removed; use insert(0, bpm) to change it).
     * Recomputes ns_at_tick for all subsequent points after removal.
     */
    void remove(uint64_t tick);

    /* Convert absolute ticks to nanoseconds from session start. */
    [[nodiscard]] uint64_t ticks_to_ns(uint64_t ticks) const;

    /* Convert nanoseconds from session start to absolute ticks. */
    [[nodiscard]] uint64_t ns_to_ticks(uint64_t ns) const;

    /*
     * Returns the tempo (BPM × 1000) active at tick — the bpm_milli of the last
     * point with tick <= the argument. Returns the first point's value for ticks
     * before it, or 0 if the map is empty (never, given the default point).
     * Integer-only; safe to call from the timing thread.
     */
    [[nodiscard]] uint32_t bpm_milli_at(uint64_t tick) const noexcept
    {
        uint32_t bpm = points_.empty() ? 0u : points_.front().bpm_milli;
        for (const auto& p : points_)
        {
            if (p.tick <= tick)
            {
                bpm = p.bpm_milli;
            }
            else
            {
                break;
            }
        }
        return bpm;
    }

    /* Raw access for serialization (non-timing-thread). */
    [[nodiscard]] const std::vector<TempoPoint>& points() const noexcept { return points_; }

private:
    static uint64_t segment_ticks_to_ns(uint64_t ticks, uint32_t bpm_milli);
    static uint64_t segment_ns_to_ticks(uint64_t ns, uint32_t bpm_milli);

    std::vector<TempoPoint> points_; /* sorted ascending by tick */
};

}  // namespace omega
