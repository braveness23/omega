#include <omega/timeline.h>

#include <algorithm>
#include <cstring>

namespace omega
{

TrackId TimelineSource::add_track(std::string name)
{
    TrackId tid = next_id_++;
    tracks_.emplace_back(tid, std::move(name));
    return tid;
}

omega_status_t TimelineSource::set_sink(TrackId track_id, uint32_t sink_id)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->sink_id = sink_id;
    return OMEGA_OK;
}

omega_status_t TimelineSource::add_event(TrackId track_id, const Event& event)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }

    auto pos = std::lower_bound(
        trk->events.begin(), trk->events.end(), event.tick, [](const Event& ev, uint64_t tick) {
            return ev.tick < tick;
        });
    trk->events.insert(pos, event);
    return OMEGA_OK;
}

omega_status_t TimelineSource::remove_event(TrackId track_id, uint64_t tick, uint32_t index)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }

    auto pos = std::lower_bound(
        trk->events.begin(), trk->events.end(), tick, [](const Event& ev, uint64_t v) {
            return ev.tick < v;
        });
    uint32_t idx = 0;
    while (pos != trk->events.end() && pos->tick == tick)
    {
        if (idx == index)
        {
            trk->events.erase(pos);
            return OMEGA_OK;
        }
        ++pos;
        ++idx;
    }
    return OMEGA_ERR_NOT_FOUND;
}

void TimelineSource::advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& /*ctx*/)
{
    const uint64_t from_tick = started_ ? next_tick_ : 0u;

    for (auto& track : tracks_)
    {
        if (track.muted || track.events.empty())
        {
            continue;
        }

        auto pos = std::lower_bound(
            track.events.begin(), track.events.end(), from_tick, [](const Event& ev, uint64_t v) {
                return ev.tick < v;
            });

        while (pos != track.events.end() && pos->tick <= to_tick)
        {
            dispatcher.dispatch(*pos);

            if (pos->payload_tag == OMEGA_NOTE_ON)
            {
                uint32_t duration = 0;
                std::memcpy(&duration, &pos->data[2], sizeof(duration));
                if (duration > 0)
                {
                    active_notes_.push_back(
                        {pos->tick + duration, pos->sink_id, pos->data[0], pos->channel});
                }
            }
            ++pos;
        }
    }

    for (auto pos = active_notes_.begin(); pos != active_notes_.end();)
    {
        if (pos->off_tick <= to_tick)
        {
            Event off{};
            off.tick = pos->off_tick;
            off.sink_id = pos->sink_id;
            off.payload_tag = OMEGA_NOTE_OFF;
            off.channel = pos->channel;
            off.data[0] = pos->note;
            off.data[1] = 64;
            dispatcher.dispatch(off);
            pos = active_notes_.erase(pos);
        }
        else
        {
            ++pos;
        }
    }

    started_ = true;
    next_tick_ = to_tick + 1;
}

void TimelineSource::on_locate(uint64_t tick,
                               EventDispatcher& /*dispatcher*/,
                               ProcessContext& /*ctx*/)
{
    started_ = true;
    next_tick_ = tick;
    active_notes_.clear();
}

Track* TimelineSource::find_track(TrackId tid) noexcept
{
    for (auto& trk : tracks_)
    {
        if (trk.id == tid)
        {
            return &trk;
        }
    }
    return nullptr;
}

const Track* TimelineSource::find_track(TrackId tid) const noexcept
{
    for (const auto& trk : tracks_)
    {
        if (trk.id == tid)
        {
            return &trk;
        }
    }
    return nullptr;
}

}  // namespace omega
