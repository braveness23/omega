#pragma once

#include <omega/omega.h>

#include <cstdint>
#include <vector>

namespace omega
{

// Returns true if v is a valid time signature denominator (power of 2, 1–32).
constexpr bool is_valid_timesig_denominator(uint8_t v) noexcept
{
    return v == 1u || v == 2u || v == 4u || v == 8u || v == 16u || v == 32u;
}

// Ticks per beat for a denominator (literal note value: 4 = quarter note, etc.).
constexpr uint64_t ticks_per_beat_for(uint8_t denominator) noexcept
{
    return (static_cast<uint64_t>(OMEGA_PPQN) * 4u) / static_cast<uint64_t>(denominator);
}

// Ticks per bar for a numerator/denominator pair.
constexpr uint64_t ticks_per_bar_for(uint8_t numerator, uint8_t denominator) noexcept
{
    return ticks_per_beat_for(denominator) * static_cast<uint64_t>(numerator);
}

struct TimeSigPoint
{
    uint64_t tick;        // tick at which this meter takes effect
    uint8_t numerator;    // beats per bar (1–99)
    uint8_t denominator;  // beat unit: 1, 2, 4, 8, 16, or 32 (literal, not exponent)
};

/*
 * Sorted list of time signature change points, parallel in structure to TempoMap.
 *
 * An empty map means the session is in freeform mode — no meter is defined.
 * Mutations must be applied from the timing thread only (via the command queue).
 *
 * Thread: not thread-safe; use the command queue for all mutations.
 */
class TimeSignatureMap
{
public:
    // Insert or replace the entry at tick.
    // Returns OMEGA_ERR_INVALID if denominator is not a power of 2 in [1, 32].
    omega_status_t insert(uint64_t tick, uint8_t numerator, uint8_t denominator);

    // Remove the entry at exactly tick.
    // Returns OMEGA_ERR_NOT_FOUND if no entry exists at that tick.
    omega_status_t remove(uint64_t tick);

    // Remove all entries, entering freeform mode.
    void clear() noexcept;

    // Returns the active TimeSigPoint at or before tick.
    // Returns nullptr if the map is empty or tick precedes the first entry.
    [[nodiscard]] const TimeSigPoint* at(uint64_t tick) const noexcept;

    [[nodiscard]] bool is_freeform() const noexcept { return points_.empty(); }
    [[nodiscard]] size_t size() const noexcept { return points_.size(); }

    // Raw access for MeterCursor and serialization (non-timing-thread).
    [[nodiscard]] const std::vector<TimeSigPoint>& points() const noexcept { return points_; }

private:
    std::vector<TimeSigPoint> points_;  // sorted ascending by tick
};

// ── Coordinate-system base interface ──────────────────────────────────────────

/*
 * Common base for all non-realtime coordinate-system helpers.
 *
 * A generic snap-to-grid function can accept PositionConverter& without
 * caring which format is active. Must not be called from the timing thread.
 */
class PositionConverter
{
public:
    PositionConverter() = default;
    virtual ~PositionConverter() = default;

    PositionConverter(const PositionConverter&) = delete;
    PositionConverter& operator=(const PositionConverter&) = delete;
    PositionConverter(PositionConverter&&) = delete;
    PositionConverter& operator=(PositionConverter&&) = delete;

    // Quantize tick to the nearest grid point in this coordinate system.
    // Returns OMEGA_ERR_NO_METER or OMEGA_ERR_NO_SMPTE_CONFIG if config is absent.
    virtual omega_status_t quantize(uint64_t tick, uint64_t& out) const = 0;

    // Tick of the next grid boundary at or after from_tick.
    virtual omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const = 0;
};

// ── Bar/beat position ─────────────────────────────────────────────────────────

struct BeatPosition
{
    uint32_t bar;          // 1-based bar number
    uint8_t beat;          // 1-based beat within bar (1..numerator)
    uint32_t subdivision;  // ticks past the beat boundary (0..ticks_per_beat - 1)
};

/*
 * Non-realtime helper for bar/beat navigation.
 *
 * Constructs from a const reference to TimeSignatureMap. The referenced map
 * must remain valid and unmodified for the lifetime of the MeterCursor.
 * Must not be called from the timing thread.
 */
class MeterCursor : public PositionConverter
{
public:
    explicit MeterCursor(const TimeSignatureMap& map) noexcept;

    // Convert absolute tick → {bar, beat, subdivision}.
    // Returns OMEGA_ERR_NO_METER if freeform or tick precedes the first entry.
    omega_status_t tick_to_beat_pos(uint64_t tick, BeatPosition& out) const;

    // Convert {bar, beat, subdivision} → absolute tick.
    // Returns OMEGA_ERR_NO_METER if freeform.
    // Returns OMEGA_ERR_INVALID if bar/beat are zero, or beat > numerator.
    omega_status_t beat_pos_to_tick(const BeatPosition& pos, uint64_t& out) const;

    // Tick of the next bar boundary at or after from_tick.
    // Returns OMEGA_ERR_NO_METER if freeform.
    omega_status_t next_bar_tick(uint64_t from_tick, uint64_t& out) const;

    // Tick of the next beat boundary at or after from_tick.
    // Returns OMEGA_ERR_NO_METER if freeform.
    omega_status_t next_beat_tick(uint64_t from_tick, uint64_t& out) const;

    // Quantize tick to the nearest beat (round-half-up).
    // Returns OMEGA_ERR_NO_METER if freeform.
    omega_status_t quantize_to_beat(uint64_t tick, uint64_t& out) const;

    // Quantize tick to the nearest subdivision of subdiv_ticks length.
    // Returns OMEGA_ERR_NO_METER if freeform. Returns OMEGA_ERR_INVALID if
    // subdiv_ticks is zero.
    omega_status_t quantize_to_subdivision(uint64_t tick,
                                           uint64_t subdiv_ticks,
                                           uint64_t& out) const;

    // PositionConverter overrides: quantize → nearest beat; next_boundary → next bar.
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;

private:
    const TimeSignatureMap& map_;
};

}  // namespace omega
