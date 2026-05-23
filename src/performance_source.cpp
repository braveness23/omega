#include <omega/perf_slot.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace omega
{

PerformanceSource::PerformanceSource(const PatternLibrary& library) noexcept : library_{library} {}

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
        slot = PerfSlot{};
        return;
    }

    switch (slot.state)
    {
        case SlotState::EMPTY:
            slot.state = SlotState::IDLE;
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
        slot.state = SlotState::PLAYING;
        return;
    }

    // CueMode::NEXT_BEAT — queue at next loop boundary
    switch (slot.state)
    {
        case SlotState::EMPTY:
            break;
        case SlotState::IDLE:
        {
            slot.pending = slot.assigned;
            slot.transition_tick = global_boundary(current_tick, len);
            slot.state = SlotState::QUEUED;
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
            if (slot.assigned == slot.playing)
            {
                slot.pending = 0u;
                slot.transition_tick = boundary;
                slot.state = SlotState::STOPPING;
            }
            else
            {
                slot.pending = slot.assigned;
                slot.transition_tick = boundary;
                slot.state = SlotState::STOPPING;
            }
            break;
        }
        case SlotState::STOPPING:
        {
            slot.pending = slot.assigned;
            // transition_tick stays: current loop boundary for the playing pattern
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

    if (mode == CueMode::IMMEDIATE)
    {
        switch (slot.state)
        {
            case SlotState::PLAYING:
                slot.pending = 0u;
                slot.transition_tick = next_tick_;
                slot.state = SlotState::STOPPING;
                break;
            case SlotState::STOPPING:
                slot.pending = 0u;
                slot.transition_tick = next_tick_;
                break;
            case SlotState::QUEUED:
                slot.active_notes.clear();
                slot.pending = 0u;
                slot.state = SlotState::IDLE;
                break;
            default:
                break;
        }
        return;
    }

    // CueMode::NEXT_BEAT — silence at next loop boundary
    switch (slot.state)
    {
        case SlotState::PLAYING:
        {
            uint64_t boundary = next_loop_boundary(slot, current_tick);
            slot.pending = 0u;
            slot.transition_tick = boundary;
            slot.state = SlotState::STOPPING;
            break;
        }
        case SlotState::QUEUED:
            slot.active_notes.clear();
            slot.pending = 0u;
            slot.state = SlotState::IDLE;
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
                        slot.rng_state = slot.rng_state * 1664525u + 1013904223u;
                        float frac =
                            static_cast<float>(slot.rng_state >> 1u) * (1.0F / 2147483648.0F);
                        if (frac < static_cast<float>(slot.random_bias) * 0.01F)
                        {
                            slot.rng_state = slot.rng_state * 1664525u + 1013904223u;
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
        switch (slot.state)
        {
            case SlotState::QUEUED:
                if (slot.transition_tick <= to_tick)
                {
                    slot.playing = slot.pending;
                    slot.start_tick = slot.transition_tick;
                    slot.pending = 0u;
                    slot.state = SlotState::PLAYING;
                    dispatch_slot_events(slot, slot.start_tick, to_tick, dispatcher);
                    fire_note_offs(slot, to_tick, dispatcher);
                }
                break;

            case SlotState::PLAYING:
                dispatch_slot_events(slot, from_tick, to_tick, dispatcher);
                fire_note_offs(slot, to_tick, dispatcher);
                break;

            case SlotState::STOPPING:
                if (slot.transition_tick <= to_tick)
                {
                    if (slot.transition_tick > from_tick)
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
                        slot.state = SlotState::PLAYING;
                        dispatch_slot_events(slot, slot.start_tick, to_tick, dispatcher);
                        fire_note_offs(slot, to_tick, dispatcher);
                    }
                    else
                    {
                        slot.playing = 0u;
                        slot.state = SlotState::IDLE;
                    }
                }
                else
                {
                    dispatch_slot_events(slot, from_tick, to_tick, dispatcher);
                    fire_note_offs(slot, to_tick, dispatcher);
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

        switch (slot.state)
        {
            case SlotState::QUEUED:
                if (slot.transition_tick <= tick)
                {
                    slot.playing = slot.pending;
                    slot.start_tick = slot.transition_tick;
                    slot.pending = 0u;
                    slot.state = SlotState::PLAYING;
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
                        slot.state = SlotState::PLAYING;
                    }
                    else
                    {
                        slot.playing = 0u;
                        slot.state = SlotState::IDLE;
                    }
                }
                break;
            default:
                break;
        }
    }

    next_tick_ = tick;
}

}  // namespace omega
