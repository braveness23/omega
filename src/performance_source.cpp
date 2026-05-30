#include <omega/perf_slot.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace omega
{

PerformanceSource::PerformanceSource(const PatternLibrary& library,
                                     const TimeSignatureMap& timesig_map) noexcept
    : library_{library}, timesig_map_{timesig_map}
{}

// ── Boundary helpers ──────────────────────────────────────────────────────────

uint64_t PerformanceSource::global_boundary(uint64_t current_tick, uint64_t length) noexcept
{
    if (length == 0)
    {
        return current_tick;
    }
    return ((current_tick + length - 1u) / length) * length;
}

uint64_t PerformanceSource::next_loop_boundary(const PerfSlot& slot,
                                               uint64_t current_tick) const noexcept
{
    const Pattern* pat = library_.get(slot.playing);
    if (pat == nullptr || pat->length_ticks == 0)
    {
        return current_tick;
    }
    uint64_t len = pat->length_ticks;
    uint64_t elapsed = current_tick >= slot.start_tick ? current_tick - slot.start_tick : 0u;
    uint64_t loop_idx = elapsed / len;
    return slot.start_tick + (loop_idx + 1u) * len;
}

// ── Mutation methods ──────────────────────────────────────────────────────────

void PerformanceSource::assign(SlotId slot_id, PatternId pattern)
{
    if (slot_id >= PERF_MAX_SLOTS)
    {
        return;
    }
    PerfSlot& slot = slots_[slot_id];

    if (pattern == 0u)
    {
        slot.reset();
        return;
    }

    auto cur = static_cast<SlotState>(slot.state.load(std::memory_order_relaxed));
    switch (cur)
    {
        case SlotState::EMPTY:
            slot.state.store(static_cast<uint8_t>(SlotState::IDLE), std::memory_order_relaxed);
            slot.assigned = pattern;
            break;
        case SlotState::IDLE:
            slot.assigned = pattern;
            break;
        case SlotState::QUEUED:
            slot.assigned = pattern;
            slot.pending = pattern;
            break;
        case SlotState::PLAYING:
        case SlotState::STOPPING:
            slot.assigned = pattern;
            break;
    }
}

// Returns the next bar boundary at or after current_tick using the session's time
// signature map. Falls back to the next loop boundary when in freeform mode.
uint64_t PerformanceSource::next_bar_boundary(uint64_t current_tick,
                                              uint64_t loop_len) const noexcept
{
    const TimeSigPoint* pt = timesig_map_.at(current_tick);
    if (pt != nullptr)
    {
        uint64_t tpbar = ticks_per_bar_for(pt->numerator, pt->denominator);
        if (tpbar > 0u)
        {
            uint64_t offset = current_tick - pt->tick;
            uint64_t bar_start = pt->tick + (offset / tpbar) * tpbar;
            return (bar_start == current_tick) ? current_tick : bar_start + tpbar;
        }
    }
    // Freeform mode: align to pattern loop boundary.
    return global_boundary(current_tick, loop_len);
}

// Queues a slot for playback according to the given cue mode.
//
// Valid state transitions (see docs/design/05-pattern-state-machine.md):
//   IDLE     → QUEUED    : schedules the assigned pattern at the boundary tick
//   QUEUED   → QUEUED    : updates the pending pattern; recalculates boundary
//   PLAYING  → STOPPING  : schedules stop (± restart if assigned != playing)
//   STOPPING → STOPPING  : updates the pending pattern for after the stop
//   EMPTY    — no-op     : slot has no pattern; nothing to cue
//
// IMMEDIATE mode bypasses boundary calculation: the slot transitions directly
// to PLAYING at next_tick_ with no QUEUED/STOPPING intermediate state.
void PerformanceSource::cue(SlotId slot_id, CueMode mode, uint64_t current_tick)
{
    if (slot_id >= PERF_MAX_SLOTS)
    {
        return;
    }
    PerfSlot& slot = slots_[slot_id];

    if (slot.assigned == 0u)
    {
        return;
    }

    const Pattern* pat = library_.get(slot.assigned);
    if (pat == nullptr || pat->length_ticks == 0u)
    {
        return;
    }

    uint64_t len = pat->length_ticks;

    if (mode == CueMode::IMMEDIATE)
    {
        slot.active_notes.clear();
        slot.playing = slot.assigned;
        slot.start_tick = next_tick_;
        slot.pending = 0u;
        slot.state.store(static_cast<uint8_t>(SlotState::PLAYING), std::memory_order_relaxed);
        return;
    }

    auto cur = static_cast<SlotState>(slot.state.load(std::memory_order_relaxed));

    if (mode == CueMode::NEXT_BAR)
    {
        uint64_t bar_tick = next_bar_boundary(current_tick, len);
        switch (cur)
        {
            case SlotState::EMPTY:
                break;
            case SlotState::IDLE:
                slot.pending = slot.assigned;
                slot.transition_tick = bar_tick;
                slot.state.store(static_cast<uint8_t>(SlotState::QUEUED),
                                 std::memory_order_relaxed);
                break;
            case SlotState::QUEUED:
                slot.pending = slot.assigned;
                slot.transition_tick = bar_tick;
                break;
            case SlotState::PLAYING:
            {
                slot.pending = (slot.assigned == slot.playing) ? 0u : slot.assigned;
                slot.transition_tick = bar_tick;
                slot.state.store(static_cast<uint8_t>(SlotState::STOPPING),
                                 std::memory_order_relaxed);
                break;
            }
            case SlotState::STOPPING:
                slot.pending = slot.assigned;
                break;
        }
        return;
    }

    // CueMode::NEXT_BEAT — queue at next loop boundary
    switch (cur)
    {
        case SlotState::EMPTY:
            break;
        case SlotState::IDLE:
        {
            slot.pending = slot.assigned;
            slot.transition_tick = global_boundary(current_tick, len);
            slot.state.store(static_cast<uint8_t>(SlotState::QUEUED), std::memory_order_relaxed);
            break;
        }
        case SlotState::QUEUED:
        {
            slot.pending = slot.assigned;
            slot.transition_tick = global_boundary(current_tick, len);
            break;
        }
        case SlotState::PLAYING:
        {
            uint64_t boundary = next_loop_boundary(slot, current_tick);
            slot.pending = (slot.assigned == slot.playing) ? 0u : slot.assigned;
            slot.transition_tick = boundary;
            slot.state.store(static_cast<uint8_t>(SlotState::STOPPING), std::memory_order_relaxed);
            break;
        }
        case SlotState::STOPPING:
        {
            slot.pending = slot.assigned;
            break;
        }
    }
}

void PerformanceSource::stop(SlotId slot_id, CueMode mode, uint64_t current_tick)
{
    if (slot_id >= PERF_MAX_SLOTS)
    {
        return;
    }
    PerfSlot& slot = slots_[slot_id];

    auto cur = static_cast<SlotState>(slot.state.load(std::memory_order_relaxed));

    if (mode == CueMode::IMMEDIATE)
    {
        switch (cur)
        {
            case SlotState::PLAYING:
                slot.pending = 0u;
                slot.transition_tick = next_tick_;
                slot.state.store(static_cast<uint8_t>(SlotState::STOPPING),
                                 std::memory_order_relaxed);
                break;
            case SlotState::STOPPING:
                slot.pending = 0u;
                slot.transition_tick = next_tick_;
                break;
            case SlotState::QUEUED:
                slot.active_notes.clear();
                slot.pending = 0u;
                slot.state.store(static_cast<uint8_t>(SlotState::IDLE), std::memory_order_relaxed);
                break;
            default:
                break;
        }
        return;
    }

    // CueMode::NEXT_BEAT — silence at next loop boundary
    switch (cur)
    {
        case SlotState::PLAYING:
        {
            uint64_t boundary = next_loop_boundary(slot, current_tick);
            slot.pending = 0u;
            slot.transition_tick = boundary;
            slot.state.store(static_cast<uint8_t>(SlotState::STOPPING), std::memory_order_relaxed);
            break;
        }
        case SlotState::QUEUED:
            slot.active_notes.clear();
            slot.pending = 0u;
            slot.state.store(static_cast<uint8_t>(SlotState::IDLE), std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

void PerformanceSource::stop_all(CueMode mode, uint64_t current_tick)
{
    for (uint32_t i = 0u; i < PERF_MAX_SLOTS; ++i)
    {
        stop(i, mode, current_tick);
    }
}

void PerformanceSource::set_transpose(SlotId slot_id, int8_t semitones)
{
    if (slot_id < PERF_MAX_SLOTS)
    {
        slots_[slot_id].transpose = semitones;
    }
}

void PerformanceSource::set_velocity_scale(SlotId slot_id, uint8_t scale)
{
    if (slot_id < PERF_MAX_SLOTS)
    {
        slots_[slot_id].velocity_scale = scale;
    }
}

void PerformanceSource::set_random_bias(SlotId slot_id, uint8_t bias)
{
    if (slot_id < PERF_MAX_SLOTS)
    {
        slots_[slot_id].random_bias = bias;
    }
}

void PerformanceSource::set_repeat_count(SlotId slot_id, uint32_t count)
{
    if (slot_id < PERF_MAX_SLOTS)
    {
        slots_[slot_id].repeat_count = count;
    }
}

void PerformanceSource::set_mute(SlotId slot_id, bool muted)
{
    if (slot_id >= PERF_MAX_SLOTS)
    {
        return;
    }
    PerfSlot& slot = slots_[slot_id];
    if (muted && !slot.muted)
    {
        slot.needs_silence = true;
    }
    slot.muted = muted;
}

// ── Event dispatch helpers ────────────────────────────────────────────────────

void PerformanceSource::dispatch_slot_events(PerfSlot& slot,
                                             uint64_t from_tick,
                                             uint64_t to_tick,
                                             EventDispatcher& dispatcher)
{
    const Pattern* pat = library_.get(slot.playing);
    if (pat == nullptr || pat->length_ticks == 0u || pat->events.empty())
    {
        return;
    }

    uint64_t len = pat->length_ticks;
    uint64_t S = slot.start_tick;

    uint64_t from = from_tick >= S ? from_tick : S;
    if (from > to_tick)
    {
        return;
    }

    // Walk through loop iterations that overlap [from, to_tick].
    //   loop_idx   — which repetition of the pattern we're examining (0-based)
    //   iter_start — absolute tick where this repetition begins
    //   iter_end   — absolute tick where this repetition ends (exclusive)
    //   window_*   — intersection of [from, to_tick] with [iter_start, iter_end-1]
    //   pat_*      — same window expressed as offsets within the pattern
    uint64_t loop_idx = (from - S) / len;

    while (true)
    {
        uint64_t iter_start = S + loop_idx * len;
        uint64_t iter_end = iter_start + len;

        if (iter_start > to_tick)
        {
            break;
        }

        uint64_t window_from = from > iter_start ? from : iter_start;
        uint64_t window_to = to_tick < iter_end - 1u ? to_tick : iter_end - 1u;

        uint64_t pat_from = window_from - iter_start;
        uint64_t pat_to = window_to - iter_start;

        auto pos = std::lower_bound(
            pat->events.begin(), pat->events.end(), pat_from, [](const Event& ev, uint64_t v) {
                return ev.tick < v;
            });

        while (pos != pat->events.end() && pos->tick <= pat_to)
        {
            Event ev = *pos;

            if (ev.payload_tag == OMEGA_NOTE_ON || ev.payload_tag == OMEGA_NOTE_OFF)
            {
                auto note = static_cast<int16_t>(static_cast<int16_t>(ev.data[0]) + slot.transpose);
                if (note < 0)
                {
                    note = 0;
                }
                else if (note > 127)
                {
                    note = 127;
                }
                ev.data[0] = static_cast<uint8_t>(note);

                if (ev.payload_tag == OMEGA_NOTE_ON)
                {
                    uint32_t vel = (static_cast<uint32_t>(ev.data[1]) * slot.velocity_scale) / 100u;
                    if (vel < 1u)
                    {
                        vel = 1u;
                    }
                    else if (vel > 127u)
                    {
                        vel = 127u;
                    }
                    ev.data[1] = static_cast<uint8_t>(vel);

                    if (slot.random_bias > 0u)
                    {
                        // LCG step (Knuth Vol.2: multiplier 1664525, addend 1013904223).
                        // Advance the state, then map the upper 31 bits to [0, 1) to get
                        // a uniform float. If it falls below the bias probability threshold,
                        // advance again and apply a pitch offset in [-5, +5] semitones.
                        slot.rng_state = slot.rng_state * 1664525u + 1013904223u;
                        float frac =
                            static_cast<float>(slot.rng_state >> 1u) * (1.0F / 2147483648.0F);
                        if (frac < static_cast<float>(slot.random_bias) * 0.01F)
                        {
                            slot.rng_state = slot.rng_state * 1664525u + 1013904223u;
                            // rng_state % 11 gives 0–10; subtracting 5 gives -5 to +5.
                            int bias_offset = static_cast<int>(slot.rng_state % 11u) - 5;
                            auto biased = static_cast<int16_t>(static_cast<int16_t>(ev.data[0]) +
                                                               bias_offset);
                            if (biased < 0)
                            {
                                biased = 0;
                            }
                            else if (biased > 127)
                            {
                                biased = 127;
                            }
                            ev.data[0] = static_cast<uint8_t>(biased);
                        }
                    }
                }
            }

            ev.tick = iter_start + pos->tick;
            dispatcher.dispatch(ev);

            if (pos->payload_tag == OMEGA_NOTE_ON)
            {
                uint32_t duration = 0u;
                std::memcpy(&duration, &pos->data[2], sizeof(duration));
                if (duration > 0u)
                {
                    slot.active_notes.push_back(
                        {ev.tick + duration, ev.sink_id, ev.data[0], ev.channel});
                }
            }
            ++pos;
        }

        if (to_tick < iter_end)
        {
            break;
        }
        ++loop_idx;
    }
}

void PerformanceSource::fire_note_offs(PerfSlot& slot,
                                       uint64_t to_tick,
                                       EventDispatcher& dispatcher)
{
    for (auto it = slot.active_notes.begin(); it != slot.active_notes.end();)
    {
        if (it->off_tick <= to_tick)
        {
            Event off{};
            off.tick = it->off_tick;
            off.sink_id = it->sink_id;
            off.payload_tag = OMEGA_NOTE_OFF;
            off.channel = it->channel;
            off.data[0] = it->note;
            off.data[1] = 64u;
            dispatcher.dispatch(off);
            it = slot.active_notes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PerformanceSource::silence_slot(PerfSlot& slot, uint64_t at_tick, EventDispatcher& dispatcher)
{
    for (const auto& n : slot.active_notes)
    {
        Event off{};
        off.tick = at_tick;
        off.sink_id = n.sink_id;
        off.payload_tag = OMEGA_NOTE_OFF;
        off.channel = n.channel;
        off.data[0] = n.note;
        off.data[1] = 64u;
        dispatcher.dispatch(off);
    }
    slot.active_notes.clear();
}

// ── advance() ─────────────────────────────────────────────────────────────────

void PerformanceSource::advance(uint64_t to_tick,
                                EventDispatcher& dispatcher,
                                ProcessContext& /*ctx*/)
{
    uint64_t from_tick = next_tick_;

    for (auto& slot : slots_)
    {
        auto cur = static_cast<SlotState>(slot.state.load(std::memory_order_relaxed));
        switch (cur)
        {
            case SlotState::QUEUED:
                if (slot.transition_tick <= to_tick)
                {
                    slot.playing = slot.pending;
                    slot.start_tick = slot.transition_tick;
                    slot.pending = 0u;
                    slot.state.store(static_cast<uint8_t>(SlotState::PLAYING),
                                     std::memory_order_relaxed);
                    if (!slot.muted)
                    {
                        dispatch_slot_events(slot, slot.start_tick, to_tick, dispatcher);
                        fire_note_offs(slot, to_tick, dispatcher);
                    }
                }
                break;

            case SlotState::PLAYING:
            {
                if (slot.needs_silence)
                {
                    silence_slot(slot, from_tick, dispatcher);
                    slot.needs_silence = false;
                }
                if (!slot.muted)
                {
                    if (slot.repeat_count > 0u)
                    {
                        const Pattern* pat = library_.get(slot.playing);
                        if (pat != nullptr && pat->length_ticks > 0u)
                        {
                            uint64_t end_tick =
                                slot.start_tick +
                                static_cast<uint64_t>(slot.repeat_count) * pat->length_ticks;
                            if (to_tick >= end_tick)
                            {
                                if (end_tick > from_tick)
                                {
                                    dispatch_slot_events(
                                        slot, from_tick, end_tick - 1u, dispatcher);
                                    fire_note_offs(slot, end_tick - 1u, dispatcher);
                                }
                                silence_slot(slot, end_tick, dispatcher);
                                slot.playing = 0u;
                                slot.state.store(static_cast<uint8_t>(SlotState::IDLE),
                                                 std::memory_order_relaxed);
                                break;
                            }
                        }
                    }
                    dispatch_slot_events(slot, from_tick, to_tick, dispatcher);
                    fire_note_offs(slot, to_tick, dispatcher);
                }
                break;
            }

            case SlotState::STOPPING:
                if (slot.transition_tick <= to_tick)
                {
                    slot.needs_silence = false;
                    if (slot.transition_tick > from_tick && !slot.muted)
                    {
                        dispatch_slot_events(
                            slot, from_tick, slot.transition_tick - 1u, dispatcher);
                        fire_note_offs(slot, slot.transition_tick - 1u, dispatcher);
                    }
                    silence_slot(slot, slot.transition_tick, dispatcher);

                    if (slot.pending != 0u)
                    {
                        slot.playing = slot.pending;
                        slot.start_tick = slot.transition_tick;
                        slot.pending = 0u;
                        slot.state.store(static_cast<uint8_t>(SlotState::PLAYING),
                                         std::memory_order_relaxed);
                        if (!slot.muted)
                        {
                            dispatch_slot_events(slot, slot.start_tick, to_tick, dispatcher);
                            fire_note_offs(slot, to_tick, dispatcher);
                        }
                    }
                    else
                    {
                        slot.playing = 0u;
                        slot.state.store(static_cast<uint8_t>(SlotState::IDLE),
                                         std::memory_order_relaxed);
                    }
                }
                else
                {
                    if (slot.needs_silence)
                    {
                        silence_slot(slot, from_tick, dispatcher);
                        slot.needs_silence = false;
                    }
                    if (!slot.muted)
                    {
                        dispatch_slot_events(slot, from_tick, to_tick, dispatcher);
                        fire_note_offs(slot, to_tick, dispatcher);
                    }
                }
                break;

            default:
                break;
        }
    }

    next_tick_ = to_tick + 1u;
}

// ── on_locate() ───────────────────────────────────────────────────────────────

void PerformanceSource::on_locate(uint64_t tick,
                                  EventDispatcher& /*dispatcher*/,
                                  ProcessContext& /*ctx*/)
{
    for (auto& slot : slots_)
    {
        slot.active_notes.clear();

        auto cur = static_cast<SlotState>(slot.state.load(std::memory_order_relaxed));
        switch (cur)
        {
            case SlotState::QUEUED:
                if (slot.transition_tick <= tick)
                {
                    slot.playing = slot.pending;
                    slot.start_tick = slot.transition_tick;
                    slot.pending = 0u;
                    slot.state.store(static_cast<uint8_t>(SlotState::PLAYING),
                                     std::memory_order_relaxed);
                }
                break;
            case SlotState::STOPPING:
                if (slot.transition_tick <= tick)
                {
                    if (slot.pending != 0u)
                    {
                        slot.playing = slot.pending;
                        slot.start_tick = slot.transition_tick;
                        slot.pending = 0u;
                        slot.state.store(static_cast<uint8_t>(SlotState::PLAYING),
                                         std::memory_order_relaxed);
                    }
                    else
                    {
                        slot.playing = 0u;
                        slot.state.store(static_cast<uint8_t>(SlotState::IDLE),
                                         std::memory_order_relaxed);
                    }
                }
                break;
            default:
                break;
        }
    }

    next_tick_ = tick;
}

SlotState PerformanceSource::slot_state(uint32_t slot) const noexcept
{
    if (slot >= PERF_MAX_SLOTS)
    {
        return SlotState::EMPTY;
    }
    return static_cast<SlotState>(slots_[slot].state.load(std::memory_order_relaxed));
}

}  // namespace omega
