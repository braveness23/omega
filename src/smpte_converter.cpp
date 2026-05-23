#include <omega/smpte_converter.h>
#include <omega/tempo_map.h>

#include <cstdint>

namespace omega
{

SmpteConverter::SmpteConverter(SmpteConfig config, const TempoMap& tempo_map) noexcept
    : config_{config}, tempo_map_{tempo_map}
{}

// ── Frame ↔ ns ────────────────────────────────────────────────────────────────

// Frame count = floor(ns * fps_numerator / (fps_denominator * 1_000_000_000))
//
// For 29.97 (30000/1001): numerator=30000, denominator=1001.
// For integral fps: numerator=fps, denominator=1.
//
// To avoid overflow for sessions up to ~24 hours (ns ≤ ~8.64e13):
//   ns * 30000 ≤ 8.64e13 * 30000 = 2.59e18 < UINT64_MAX (1.84e19) — safe.
//
// For very long sessions (>~7 hours at 29.97), intermediate products exceed
// UINT64_MAX. In that case we fall back to __uint128_t where available.

uint64_t SmpteConverter::ns_to_frame(uint64_t ns) const noexcept
{
    if (config_.is_2997)
    {
        // 30000/1001 fps
        constexpr uint64_t k_safe_ns = 0xFFFFFFFFFFFFFFFFULL / 30000u;
        if (ns <= k_safe_ns)
        {
            return (ns * 30000u) / (1001u * 1'000'000'000ULL);
        }
#if defined(__SIZEOF_INT128__)
        return static_cast<uint64_t>((static_cast<__uint128_t>(ns) * 30000u) /
                                     (1001u * 1'000'000'000ULL));
#else
        // MSVC fallback: double precision is sufficient for sessions up to ~100 hours.
        return static_cast<uint64_t>(static_cast<double>(ns) * 30000.0 /
                                     (1001.0 * 1'000'000'000.0));
#endif
    }
    // Integral fps: fps / 1
    return (ns * static_cast<uint64_t>(config_.fps)) / 1'000'000'000ULL;
}

// ns = ceil(frame * fps_denominator * 1_000_000_000 / fps_numerator)
//
// Ceiling is intentional: this function is used to find the tick at or after
// the frame boundary (for smpte_to_tick and next_boundary). Floor would place
// the computed tick just before the frame start due to integer truncation in
// the tick ↔ ns conversion chain.
uint64_t SmpteConverter::frame_to_ns(uint64_t frame) const noexcept
{
    if (config_.is_2997)
    {
        // ceil(frame * 1001 * 1_000_000_000 / 30000)
        constexpr uint64_t k_denom = 30000u;
        constexpr uint64_t k_safe_frame =
            (0xFFFFFFFFFFFFFFFFULL - k_denom + 1u) / (1001u * 1'000'000'000ULL);
        if (frame <= k_safe_frame)
        {
            return (frame * 1001u * 1'000'000'000ULL + k_denom - 1u) / k_denom;
        }
#if defined(__SIZEOF_INT128__)
        return static_cast<uint64_t>(
            (static_cast<__uint128_t>(frame) * 1001u * 1'000'000'000ULL + k_denom - 1u) / k_denom);
#else
        return static_cast<uint64_t>(static_cast<double>(frame) * 1001.0 * 1'000'000'000.0 /
                                     30000.0);
#endif
    }
    // ceil(frame * 1e9 / fps)
    const uint64_t fps = static_cast<uint64_t>(config_.fps);
    return (frame * 1'000'000'000ULL + fps - 1u) / fps;
}

// ── NDF conversion ────────────────────────────────────────────────────────────

void SmpteConverter::frame_to_smpte_ndf(uint64_t frame, uint8_t fps, SmpteTime& out) noexcept
{
    uint64_t total_seconds = frame / static_cast<uint64_t>(fps);
    out.frames = static_cast<uint8_t>(frame % static_cast<uint64_t>(fps));
    out.seconds = static_cast<uint8_t>(total_seconds % 60u);
    uint64_t total_minutes = total_seconds / 60u;
    out.minutes = static_cast<uint8_t>(total_minutes % 60u);
    out.hours = static_cast<uint8_t>(total_minutes / 60u);
}

omega_status_t SmpteConverter::smpte_ndf_to_frame(const SmpteTime& t,
                                                  uint8_t fps,
                                                  uint64_t& out) noexcept
{
    if (t.seconds > 59u || t.minutes > 59u || t.frames >= fps)
    {
        return OMEGA_ERR_INVALID;
    }
    uint64_t total_seconds =
        static_cast<uint64_t>(t.hours) * 3600u + static_cast<uint64_t>(t.minutes) * 60u + t.seconds;
    out = total_seconds * static_cast<uint64_t>(fps) + t.frames;
    return OMEGA_OK;
}

// ── Drop-frame conversion ─────────────────────────────────────────────────────
//
// 29.97 DF: 30 frames/s (nominal), drop frame labels 0 and 1 at the start of
// every minute except every 10th. This gives 17982 unique addresses per 10 min.

void SmpteConverter::frame_to_smpte_df(uint64_t frame, SmpteTime& out) noexcept
{
    // Frames per 10-minute group: 30*600 - 18 dropped = 17982
    constexpr uint64_t k_frames_per_10min = 17982u;

    uint64_t groups_of_10 = frame / k_frames_per_10min;
    uint64_t r = frame % k_frames_per_10min;

    uint64_t total_minutes = groups_of_10 * 10u;
    uint32_t secs;
    uint8_t f;

    if (r < 1800u)
    {
        // First minute of the group: no frames dropped
        secs = static_cast<uint32_t>(r / 30u);
        f = static_cast<uint8_t>(r % 30u);
    }
    else
    {
        r -= 1800u;
        total_minutes += r / 1798u + 1u;
        uint64_t r2 = r % 1798u;
        // Frames 0 and 1 are skipped at start of each non-round minute, so
        // the first frame is labelled 2. Offset by 2 to get the frame label.
        secs = static_cast<uint32_t>((r2 + 2u) / 30u);
        f = static_cast<uint8_t>((r2 + 2u) % 30u);
    }

    out.frames = f;
    out.seconds = static_cast<uint8_t>(secs % 60u);
    out.minutes = static_cast<uint8_t>(total_minutes % 60u);
    out.hours = static_cast<uint8_t>(total_minutes / 60u);
}

omega_status_t SmpteConverter::smpte_df_to_frame(const SmpteTime& t, uint64_t& out) noexcept
{
    if (t.seconds > 59u || t.minutes > 59u || t.frames >= 30u)
    {
        return OMEGA_ERR_INVALID;
    }

    uint64_t total_minutes =
        static_cast<uint64_t>(t.hours) * 60u + static_cast<uint64_t>(t.minutes);

    // Frames 0 and 1 at S=0 of every non-round minute are illegal DF addresses.
    bool is_round_minute = (total_minutes % 10u == 0u);
    if (!is_round_minute && t.seconds == 0u && t.frames < 2u)
    {
        return OMEGA_ERR_INVALID;
    }

    // Standard NTSC drop-frame formula:
    //   frame = 108000*H + 1800*M + 30*S + F - 2*(total_minutes - total_minutes/10)
    uint64_t dropped = 2u * (total_minutes - total_minutes / 10u);
    out = 108000u * t.hours + 1800u * t.minutes + 30u * t.seconds + t.frames - dropped;
    return OMEGA_OK;
}

// ── Public interface ──────────────────────────────────────────────────────────

omega_status_t SmpteConverter::tick_to_smpte(uint64_t tick, SmpteTime& out) const
{
    if (!is_valid_smpte_config(config_))
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }

    uint64_t ns = tempo_map_.ticks_to_ns(tick);
    uint64_t frame = ns_to_frame(ns);

    if (config_.drop_frame)
    {
        frame_to_smpte_df(frame, out);
    }
    else
    {
        frame_to_smpte_ndf(frame, config_.fps, out);
    }
    return OMEGA_OK;
}

omega_status_t SmpteConverter::smpte_to_tick(const SmpteTime& t, uint64_t& out) const
{
    if (!is_valid_smpte_config(config_))
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }

    uint64_t frame = 0;
    omega_status_t s;
    if (config_.drop_frame)
    {
        s = smpte_df_to_frame(t, frame);
    }
    else
    {
        s = smpte_ndf_to_frame(t, config_.fps, frame);
    }
    if (s != OMEGA_OK)
    {
        return s;
    }

    uint64_t ns = frame_to_ns(frame);
    out = tempo_map_.ns_to_ticks(ns);
    return OMEGA_OK;
}

omega_status_t SmpteConverter::next_boundary(uint64_t from_tick, uint64_t& out) const
{
    if (!is_valid_smpte_config(config_))
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }

    uint64_t ns = tempo_map_.ticks_to_ns(from_tick);
    uint64_t frame = ns_to_frame(ns);
    uint64_t frame_start_ns = frame_to_ns(frame);

    // If from_tick is not on a frame boundary, advance to the next frame.
    uint64_t frame_start_tick = tempo_map_.ns_to_ticks(frame_start_ns);
    if (frame_start_tick < from_tick)
    {
        ++frame;
    }

    out = tempo_map_.ns_to_ticks(frame_to_ns(frame));
    return OMEGA_OK;
}

omega_status_t SmpteConverter::quantize(uint64_t tick, uint64_t& out) const
{
    if (!is_valid_smpte_config(config_))
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }

    uint64_t ns = tempo_map_.ticks_to_ns(tick);
    uint64_t frame = ns_to_frame(ns);

    // Check which frame boundary (current or next) is closer (round-half-up).
    uint64_t frame_ns = frame_to_ns(frame);
    uint64_t next_frame_ns = frame_to_ns(frame + 1u);
    uint64_t half = (next_frame_ns - frame_ns + 1u) / 2u;

    if (ns - frame_ns >= half)
    {
        out = tempo_map_.ns_to_ticks(next_frame_ns);
    }
    else
    {
        out = tempo_map_.ns_to_ticks(frame_ns);
    }
    return OMEGA_OK;
}

}  // namespace omega
