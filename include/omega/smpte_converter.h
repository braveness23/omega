#pragma once

#include <omega/omega.h>
#include <omega/time_signature_map.h>  // for PositionConverter

#include <cstdint>

namespace omega
{
class TempoMap;
}

namespace omega
{

// ── SmpteConfig ───────────────────────────────────────────────────────────────

/*
 * Configures the SMPTE frame-rate for the session.
 *
 *   fps        — 24, 25, or 30 (for 29.97: set fps=30 and is_2997=true)
 *   drop_frame — enables drop-frame addressing; only valid when is_2997=true
 *   is_2997    — true = 30000/1001 time base; fps must be 30
 *
 * Thread: set via command queue; read from mutation thread or non-timing query code.
 */
struct SmpteConfig
{
    uint8_t fps{30};
    bool drop_frame{false};
    bool is_2997{false};
};

// Returns true if the config values are valid (fps in {24,25,30}, is_2997 requires fps==30,
// drop_frame requires is_2997).
inline constexpr bool is_valid_smpte_config(const SmpteConfig& c) noexcept
{
    if (c.fps != 24u && c.fps != 25u && c.fps != 30u)
    {
        return false;
    }
    if (c.is_2997 && c.fps != 30u)
    {
        return false;
    }
    if (c.drop_frame && !c.is_2997)
    {
        return false;
    }
    return true;
}

// ── SmpteTime ─────────────────────────────────────────────────────────────────

struct SmpteTime
{
    uint8_t hours{0};
    uint8_t minutes{0};
    uint8_t seconds{0};
    uint8_t frames{0};
};

// ── SmpteConverter ────────────────────────────────────────────────────────────

/*
 * Non-realtime helper for SMPTE timecode conversion.
 *
 * Conversion path: tick → ns (via TempoMap) → frame number → HH:MM:SS:FF.
 * This is the query side — it does not generate MTC clock messages.
 *
 * The referenced TempoMap and SmpteConfig must remain valid and unmodified
 * for the lifetime of this converter. Must not be called from the timing thread.
 */
class SmpteConverter : public PositionConverter
{
public:
    // config is copied; tempo_map is held by reference and must outlive this object.
    SmpteConverter(SmpteConfig config, const TempoMap& tempo_map) noexcept;

    // Convert absolute tick → HH:MM:SS:FF.
    // Returns OMEGA_ERR_NO_SMPTE_CONFIG if config is invalid.
    omega_status_t tick_to_smpte(uint64_t tick, SmpteTime& out) const;

    // Convert HH:MM:SS:FF → absolute tick.
    // Returns OMEGA_ERR_NO_SMPTE_CONFIG if config is invalid.
    // Returns OMEGA_ERR_INVALID if the SMPTE address is illegal (e.g. frame 0/1
    // at the start of a non-round minute in drop-frame mode).
    omega_status_t smpte_to_tick(const SmpteTime& t, uint64_t& out) const;

    // Tick of the next frame boundary at or after from_tick.
    // Returns OMEGA_ERR_NO_SMPTE_CONFIG if config is invalid.
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;

    // Quantize tick to the nearest frame boundary (round-half-up).
    // Returns OMEGA_ERR_NO_SMPTE_CONFIG if config is invalid.
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;

private:
    SmpteConfig config_;
    const TempoMap& tempo_map_;

    // Convert nanoseconds to frame number (floor).
    [[nodiscard]] uint64_t ns_to_frame(uint64_t ns) const noexcept;

    // Convert frame number to nanoseconds (exact start of that frame).
    [[nodiscard]] uint64_t frame_to_ns(uint64_t frame) const noexcept;

    static void frame_to_smpte_ndf(uint64_t frame, uint8_t fps, SmpteTime& out) noexcept;
    static void frame_to_smpte_df(uint64_t frame, SmpteTime& out) noexcept;

    static omega_status_t smpte_ndf_to_frame(const SmpteTime& t,
                                             uint8_t fps,
                                             uint64_t& out) noexcept;
    static omega_status_t smpte_df_to_frame(const SmpteTime& t, uint64_t& out) noexcept;
};

}  // namespace omega
