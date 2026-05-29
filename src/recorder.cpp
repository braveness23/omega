#include <omega/input_bus.h>
#include <omega/omega.h>
#include <omega/process_context.h>
#include <omega/recorder.h>
#include <omega/timeline.h>
#include <omega/types.h>

#include <cstring>

namespace omega
{

Recorder::Recorder(TimelineSource& timeline, uint32_t sink_id) noexcept
    : timeline_{timeline}, sink_id_{sink_id}
{}

void Recorder::start_recording(TrackId target, uint8_t channel_filter) noexcept
{
    target_ = target;
    channel_filter_ = channel_filter;
    events_captured_ = 0;
    last_tick_ = 0;
    // Clear any leftover pending notes from a previous take.
    for (auto& ch : pending_)
    {
        for (auto& note : ch)
        {
            note = {};
        }
    }
    recording_.store(true, std::memory_order_release);
}

size_t Recorder::stop_recording() noexcept
{
    recording_.store(false, std::memory_order_release);
    flush_pending(last_tick_);
    return events_captured_;
}

void Recorder::flush_pending(uint64_t stop_tick) noexcept
{
    for (uint8_t ch = 0; ch < 16u; ++ch)
    {
        for (uint8_t note = 0; note < 128u; ++note)
        {
            PendingNote& pn = pending_[ch][note];
            if (!pn.active)
            {
                continue;
            }
            uint64_t off_tick = (stop_tick > pn.on_tick) ? stop_tick : pn.on_tick + 1u;
            auto duration = static_cast<uint32_t>(off_tick - pn.on_tick);

            Event note_ev{};
            note_ev.tick = pn.on_tick;
            note_ev.sink_id = sink_id_;
            note_ev.payload_tag = OMEGA_NOTE_ON;
            note_ev.channel = ch;
            note_ev.data[0] = note;
            note_ev.data[1] = pn.velocity;
            std::memcpy(&note_ev.data[2], &duration, sizeof(duration));

            timeline_.add_event(target_, note_ev);
            ++events_captured_;
            pn = {};
        }
    }
}

void Recorder::advance(uint64_t to_tick, EventDispatcher& /*dispatcher*/, ProcessContext& ctx)
{
    last_tick_ = to_tick;

    if (!recording_.load(std::memory_order_relaxed))
    {
        return;
    }
    if (ctx.input_bus == nullptr)
    {
        return;
    }

    const uint32_t n = ctx.input_bus->count();
    for (uint32_t i = 0; i < n; ++i)
    {
        Event ev = ctx.input_bus->at(i);

        if (channel_filter_ != 0xFFu && ev.channel != channel_filter_)
        {
            continue;
        }

        ev.sink_id = sink_id_;
        ev.tick = to_tick;

        if (ev.payload_tag == OMEGA_NOTE_ON && ev.data[1] > 0u)
        {
            // Store pending; the NOTE_ON is only inserted when its NOTE_OFF arrives.
            pending_[ev.channel & 0x0Fu][ev.data[0] & 0x7Fu] = {to_tick, ev.data[1], true};
        }
        else if (ev.payload_tag == OMEGA_NOTE_OFF ||
                 (ev.payload_tag == OMEGA_NOTE_ON && ev.data[1] == 0u))
        {
            // NOTE_ON with velocity 0 is a MIDI note-off.
            PendingNote& pn = pending_[ev.channel & 0x0Fu][ev.data[0] & 0x7Fu];
            if (pn.active)
            {
                uint64_t off_tick = (to_tick > pn.on_tick) ? to_tick : pn.on_tick + 1u;
                auto duration = static_cast<uint32_t>(off_tick - pn.on_tick);

                Event note_ev{};
                note_ev.tick = pn.on_tick;
                note_ev.sink_id = sink_id_;
                note_ev.payload_tag = OMEGA_NOTE_ON;
                note_ev.channel = ev.channel;
                note_ev.data[0] = ev.data[0];
                note_ev.data[1] = pn.velocity;
                std::memcpy(&note_ev.data[2], &duration, sizeof(duration));

                timeline_.add_event(target_, note_ev);
                ++events_captured_;
                pn = {};
            }
        }
        else if (ev.payload_tag == OMEGA_CC || ev.payload_tag == OMEGA_PROGRAM)
        {
            // CC and program changes are stored immediately (no duration to resolve).
            timeline_.add_event(target_, ev);
            ++events_captured_;
        }
    }
}

void Recorder::on_locate(uint64_t tick, EventDispatcher& /*dispatcher*/, ProcessContext& /*ctx*/)
{
    last_tick_ = tick;
    // Clear pending notes on locate so stuck-note state doesn't carry across a loop wrap.
    for (auto& ch : pending_)
    {
        for (auto& note : ch)
        {
            note = {};
        }
    }
}

}  // namespace omega
