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

omega_status_t TimelineSource::set_channel(TrackId track_id, uint8_t channel)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->channel = channel;
    return OMEGA_OK;
}

omega_status_t TimelineSource::set_name(TrackId track_id, std::string name)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->name = std::move(name);
    return OMEGA_OK;
}

omega_status_t TimelineSource::set_track_mute(TrackId track_id, bool muted)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->muted = muted;
    return OMEGA_OK;
}

omega_status_t TimelineSource::set_track_solo(TrackId track_id, bool soloed)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->soloed = soloed;
    return OMEGA_OK;
}

bool TimelineSource::track_is_muted(TrackId track_id) const noexcept
{
    const Track* trk = find_track(track_id);
    return trk != nullptr && trk->muted;
}

bool TimelineSource::track_is_soloed(TrackId track_id) const noexcept
{
    const Track* trk = find_track(track_id);
    return trk != nullptr && trk->soloed;
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

omega_status_t TimelineSource::replace_event(TrackId track_id,
                                             uint64_t tick,
                                             uint32_t index,
                                             const Event& replacement)
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
            *pos = replacement;
            // Re-sort only if the tick changed; otherwise the order is still valid.
            if (replacement.tick != tick)
            {
                std::stable_sort(trk->events.begin(),
                                 trk->events.end(),
                                 [](const Event& a, const Event& b) { return a.tick < b.tick; });
            }
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

    bool any_soloed = false;
    for (const auto& track : tracks_)
    {
        if (track.soloed)
        {
            any_soloed = true;
            break;
        }
    }

    for (auto& track : tracks_)
    {
        // A track is silent if explicitly muted, or if some track is soloed and
        // this one is not. Already-scheduled note-offs (active_notes_) still
        // fire below regardless, so muting never leaves a hanging note.
        const bool silent = track.muted || (any_soloed && !track.soloed);
        if (silent || track.events.empty())
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
                // NOTE_ON events store a 4-byte duration at data[2..5] (see omega_make_note_on
                // in types.cpp). A non-zero duration means we must synthesise the matching
                // NOTE_OFF; track it in active_notes_ rather than the event stream.
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

    // Emit any pending note-offs whose off_tick has arrived.
    // active_notes_ is a separate table from the event stream; notes are added
    // here when a NOTE_ON with a non-zero duration is dispatched, and removed
    // once the corresponding NOTE_OFF has been sent.
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
