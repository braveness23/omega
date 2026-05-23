#include <omega/time_signature_map.h>

#include <algorithm>

namespace omega
{

// ── TimeSignatureMap ──────────────────────────────────────────────────────────

omega_status_t TimeSignatureMap::insert(uint64_t tick, uint8_t numerator, uint8_t denominator)
{
    if (!is_valid_timesig_denominator(denominator) || numerator == 0u)
    {
        return OMEGA_ERR_INVALID;
    }

    auto it = std::lower_bound(
        points_.begin(), points_.end(), tick, [](const TimeSigPoint& p, uint64_t t) {
            return p.tick < t;
        });

    if (it != points_.end() && it->tick == tick)
    {
        it->numerator = numerator;
        it->denominator = denominator;
    }
    else
    {
        points_.insert(it, {tick, numerator, denominator});
    }

    return OMEGA_OK;
}

omega_status_t TimeSignatureMap::remove(uint64_t tick)
{
    auto it = std::lower_bound(
        points_.begin(), points_.end(), tick, [](const TimeSigPoint& p, uint64_t t) {
            return p.tick < t;
        });

    if (it == points_.end() || it->tick != tick)
    {
        return OMEGA_ERR_NOT_FOUND;
    }

    points_.erase(it);
    return OMEGA_OK;
}

void TimeSignatureMap::clear() noexcept
{
    points_.clear();
}

const TimeSigPoint* TimeSignatureMap::at(uint64_t tick) const noexcept
{
    if (points_.empty())
    {
        return nullptr;
    }

    auto it = std::upper_bound(
        points_.begin(), points_.end(), tick, [](uint64_t t, const TimeSigPoint& p) {
            return t < p.tick;
        });

    if (it == points_.begin())
    {
        return nullptr;
    }

    --it;
    return &*it;
}

// ── MeterCursor ───────────────────────────────────────────────────────────────

MeterCursor::MeterCursor(const TimeSignatureMap& map) noexcept : map_{map} {}

omega_status_t MeterCursor::tick_to_beat_pos(uint64_t tick, BeatPosition& out) const
{
    const auto& pts = map_.points();
    if (pts.empty() || tick < pts[0].tick)
    {
        return OMEGA_ERR_NO_METER;
    }

    uint64_t accumulated_bars = 0;

    for (size_t i = 0; i < pts.size(); ++i)
    {
        const auto& seg = pts[i];
        uint64_t tpbar = ticks_per_bar_for(seg.numerator, seg.denominator);
        uint64_t tpbeat = ticks_per_beat_for(seg.denominator);
        bool is_last = (i + 1 >= pts.size());

        if (is_last || tick < pts[i + 1].tick)
        {
            // tick lives in this segment
            uint64_t offset = tick - seg.tick;
            uint64_t bars_past_seg = offset / tpbar;
            uint64_t bar_offset = offset % tpbar;

            out.bar = static_cast<uint32_t>(accumulated_bars + bars_past_seg + 1u);
            out.beat = static_cast<uint8_t>(bar_offset / tpbeat + 1u);
            out.subdivision = static_cast<uint32_t>(bar_offset % tpbeat);
            return OMEGA_OK;
        }

        // Accumulate bars used by this completed segment (ceiling division — a
        // partial bar at the end of a segment counts as one bar).
        uint64_t seg_span = pts[i + 1].tick - seg.tick;
        accumulated_bars += (seg_span + tpbar - 1u) / tpbar;
    }

    return OMEGA_ERR_NO_METER;
}

omega_status_t MeterCursor::beat_pos_to_tick(const BeatPosition& pos, uint64_t& out) const
{
    if (map_.is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }
    if (pos.bar == 0u || pos.beat == 0u)
    {
        return OMEGA_ERR_INVALID;
    }

    const auto& pts = map_.points();
    uint64_t accumulated_bars = 0;

    for (size_t i = 0; i < pts.size(); ++i)
    {
        const auto& seg = pts[i];
        uint64_t tpbar = ticks_per_bar_for(seg.numerator, seg.denominator);
        uint64_t tpbeat = ticks_per_beat_for(seg.denominator);
        bool is_last = (i + 1 >= pts.size());

        uint64_t bars_in_seg;
        if (is_last)
        {
            bars_in_seg = UINT64_MAX;
        }
        else
        {
            uint64_t seg_span = pts[i + 1].tick - seg.tick;
            bars_in_seg = (seg_span + tpbar - 1u) / tpbar;
        }

        bool bar_in_this_seg =
            is_last || (accumulated_bars + bars_in_seg >= static_cast<uint64_t>(pos.bar));

        if (bar_in_this_seg)
        {
            if (pos.beat > seg.numerator)
            {
                return OMEGA_ERR_INVALID;
            }
            uint64_t bar_within_seg = static_cast<uint64_t>(pos.bar) - accumulated_bars - 1u;
            out = seg.tick + bar_within_seg * tpbar +
                  (static_cast<uint64_t>(pos.beat) - 1u) * tpbeat +
                  static_cast<uint64_t>(pos.subdivision);
            return OMEGA_OK;
        }

        accumulated_bars += bars_in_seg;
    }

    return OMEGA_ERR_NO_METER;
}

omega_status_t MeterCursor::next_bar_tick(uint64_t from_tick, uint64_t& out) const
{
    if (map_.is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }
    const auto& pts = map_.points();
    if (from_tick < pts[0].tick)
    {
        return OMEGA_ERR_NO_METER;
    }

    const TimeSigPoint* seg = map_.at(from_tick);
    if (seg == nullptr)
    {
        return OMEGA_ERR_NO_METER;
    }

    uint64_t tpbar = ticks_per_bar_for(seg->numerator, seg->denominator);
    uint64_t offset = from_tick - seg->tick;
    uint64_t bar_start = seg->tick + (offset / tpbar) * tpbar;
    uint64_t candidate = (bar_start == from_tick) ? from_tick : bar_start + tpbar;

    // If the next time-sig segment starts before the candidate bar boundary, treat
    // the new-segment start as a bar boundary (time-sig changes define bar edges).
    auto next_it =
        std::upper_bound(pts.begin(), pts.end(), from_tick, [](uint64_t t, const TimeSigPoint& p) {
            return t < p.tick;
        });
    if (next_it != pts.end() && next_it->tick < candidate)
    {
        candidate = next_it->tick;
    }

    out = candidate;
    return OMEGA_OK;
}

omega_status_t MeterCursor::next_beat_tick(uint64_t from_tick, uint64_t& out) const
{
    if (map_.is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }
    const auto& pts = map_.points();
    if (from_tick < pts[0].tick)
    {
        return OMEGA_ERR_NO_METER;
    }

    const TimeSigPoint* seg = map_.at(from_tick);
    if (seg == nullptr)
    {
        return OMEGA_ERR_NO_METER;
    }

    uint64_t tpbeat = ticks_per_beat_for(seg->denominator);
    uint64_t offset = from_tick - seg->tick;
    uint64_t beat_start = seg->tick + (offset / tpbeat) * tpbeat;
    uint64_t candidate = (beat_start == from_tick) ? from_tick : beat_start + tpbeat;

    // Clamp to next time-sig segment if it starts before the candidate.
    auto next_it =
        std::upper_bound(pts.begin(), pts.end(), from_tick, [](uint64_t t, const TimeSigPoint& p) {
            return t < p.tick;
        });
    if (next_it != pts.end() && next_it->tick < candidate)
    {
        candidate = next_it->tick;
    }

    out = candidate;
    return OMEGA_OK;
}

omega_status_t MeterCursor::quantize_to_beat(uint64_t tick, uint64_t& out) const
{
    if (map_.is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }
    const auto& pts = map_.points();
    if (pts.empty() || tick < pts[0].tick)
    {
        return OMEGA_ERR_NO_METER;
    }

    const TimeSigPoint* seg = map_.at(tick);
    if (seg == nullptr)
    {
        return OMEGA_ERR_NO_METER;
    }

    uint64_t tpbeat = ticks_per_beat_for(seg->denominator);
    uint64_t offset = tick - seg->tick;
    uint64_t beat_idx = offset / tpbeat;
    uint64_t remainder = offset % tpbeat;

    if (remainder * 2u >= tpbeat)
    {
        out = seg->tick + (beat_idx + 1u) * tpbeat;
    }
    else
    {
        out = seg->tick + beat_idx * tpbeat;
    }
    return OMEGA_OK;
}

omega_status_t MeterCursor::quantize_to_subdivision(uint64_t tick,
                                                    uint64_t subdiv_ticks,
                                                    uint64_t& out) const
{
    if (subdiv_ticks == 0u)
    {
        return OMEGA_ERR_INVALID;
    }
    if (map_.is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }
    const auto& pts = map_.points();
    if (pts.empty() || tick < pts[0].tick)
    {
        return OMEGA_ERR_NO_METER;
    }

    const TimeSigPoint* seg = map_.at(tick);
    if (seg == nullptr)
    {
        return OMEGA_ERR_NO_METER;
    }

    uint64_t offset = tick - seg->tick;
    uint64_t sub_idx = offset / subdiv_ticks;
    uint64_t remainder = offset % subdiv_ticks;

    if (remainder * 2u >= subdiv_ticks)
    {
        out = seg->tick + (sub_idx + 1u) * subdiv_ticks;
    }
    else
    {
        out = seg->tick + sub_idx * subdiv_ticks;
    }
    return OMEGA_OK;
}

omega_status_t MeterCursor::quantize(uint64_t tick, uint64_t& out) const
{
    return quantize_to_beat(tick, out);
}

omega_status_t MeterCursor::next_boundary(uint64_t from_tick, uint64_t& out) const
{
    return next_bar_tick(from_tick, out);
}

}  // namespace omega
