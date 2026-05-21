#include <omega/timeline.h>

#include <algorithm>
#include <cstring>

namespace omega
{

TrackId TimelineSource::add_track(std::string name)
{
    TrackId id = next_id_++;
    tracks_.emplace_back(id, std::move(name));
    return id;
}

omega_status_t TimelineSource::set_sink(TrackId track_id, uint32_t sink_id)
{
    Track* t = find_track(track_id);
    if (!t)
        return OMEGA_ERR_NOT_FOUND;
    t->sink_id = sink_id;
    return OMEGA_OK;
}

omega_status_t TimelineSource::add_event(TrackId track_id, const Event& event)
{
    Track* t = find_track(track_id);
    if (!t)
        return OMEGA_ERR_NOT_FOUND;

    auto it =
        std::lower_bound(t->events.begin(), t->events.end(), event.tick,
                         [](const Event& e, uint64_t tick) { return e.tick < tick; });
    t->events.insert(it, event);
    return OMEGA_OK;
}

omega_status_t TimelineSource::remove_event(TrackId track_id, uint64_t tick, uint32_t index)
{
    Track* t = find_track(track_id);
    if (!t)
        return OMEGA_ERR_NOT_FOUND;

    auto it = std::lower_bound(t->events.begin(), t->events.end(), tick,
                               [](const Event& e, uint64_t v) { return e.tick < v; });
    uint32_t idx = 0;
    while (it != t->events.end() && it->tick == tick)
    {
        if (idx == index)
        {
            t->events.erase(it);
            return OMEGA_OK;
        }
        ++it;
        ++idx;
    }
    return OMEGA_ERR_NOT_FOUND;
}

void TimelineSource::advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext&)
{
    const uint64_t from_tick = started_ ? next_tick_ : 0u;

    for (auto& track : tracks_)
    {
        if (track.muted || track.events.empty())
            continue;

        // Find first event in [from_tick, to_tick].
        auto it = std::lower_bound(track.events.begin(), track.events.end(), from_tick,
                                   [](const Event& e, uint64_t v) { return e.tick < v; });

        while (it != track.events.end() && it->tick <= to_tick)
        {
            dispatcher.dispatch(*it);

            // Schedule note-off if this is a note-on with non-zero duration.
            if (it->payload_tag == OMEGA_NOTE_ON)
            {
                uint32_t duration = 0;
                std::memcpy(&duration, &it->data[2], sizeof(duration));
                if (duration > 0)
                {
                    active_notes_.push_back(
                        {it->tick + duration, it->sink_id, it->data[0], it->channel});
                }
            }
            ++it;
        }
    }

    // Dispatch pending note-offs that have reached their deadline.
    for (auto it = active_notes_.begin(); it != active_notes_.end();)
    {
        if (it->off_tick <= to_tick)
        {
            Event off{};
            off.tick = it->off_tick;
            off.sink_id = it->sink_id;
            off.payload_tag = OMEGA_NOTE_OFF;
            off.channel = it->channel;
            off.data[0] = it->note;
            off.data[1] = 64;  // standard note-off velocity
            dispatcher.dispatch(off);
            it = active_notes_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    started_ = true;
    next_tick_ = to_tick + 1;
}

void TimelineSource::on_locate(uint64_t tick, EventDispatcher& /*dispatcher*/,
                               ProcessContext& /*ctx*/)
{
    started_ = true;
    next_tick_ = tick;
    active_notes_.clear();
}

Track* TimelineSource::find_track(TrackId id) noexcept
{
    for (auto& t : tracks_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Track* TimelineSource::find_track(TrackId id) const noexcept
{
    for (const auto& t : tracks_)
        if (t.id == id)
            return &t;
    return nullptr;
}

}  // namespace omega
