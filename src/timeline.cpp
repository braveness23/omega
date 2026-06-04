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

omega_status_t TimelineSource::add_meta(TrackId track_id, MetaEvent meta)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->meta.push_back(std::move(meta));
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

void TimelineSource::insert_sorted(Track& trk, const Event& event, omega_event_id_t id)
{
    // lower_bound inserts before all events sharing the same tick, so a newly
    // added event always lands at within-tick index 0 (matched by the legacy
    // index-based undo paths in engine.cpp).
    auto pos = std::lower_bound(
        trk.events.begin(), trk.events.end(), event.tick, [](const Event& ev, uint64_t tick) {
            return ev.tick < tick;
        });
    auto offset = pos - trk.events.begin();
    trk.events.insert(trk.events.begin() + offset, event);
    trk.ids.insert(trk.ids.begin() + offset, id);
}

int64_t TimelineSource::find_id_offset(const Track& trk, omega_event_id_t id) noexcept
{
    for (size_t i = 0; i < trk.ids.size(); ++i)
    {
        if (trk.ids[i] == id)
        {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

omega_status_t TimelineSource::add_event(TrackId track_id,
                                         const Event& event,
                                         omega_event_id_t* out_id)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }

    const omega_event_id_t id = next_event_id_++;
    insert_sorted(*trk, event, id);
    if (out_id != nullptr)
    {
        *out_id = id;
    }
    return OMEGA_OK;
}

omega_status_t TimelineSource::insert_event_with_id(TrackId track_id,
                                                    omega_event_id_t id,
                                                    const Event& event)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    // id is an already-issued value (< next_event_id_); do not bump the counter.
    insert_sorted(*trk, event, id);
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
            auto offset = pos - trk->events.begin();
            trk->events.erase(trk->events.begin() + offset);
            trk->ids.erase(trk->ids.begin() + offset);
            return OMEGA_OK;
        }
        ++pos;
        ++idx;
    }
    return OMEGA_ERR_NOT_FOUND;
}

omega_status_t TimelineSource::remove_event_by_id(TrackId track_id, omega_event_id_t id)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    const int64_t off = find_id_offset(*trk, id);
    if (off < 0)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    trk->events.erase(trk->events.begin() + off);
    trk->ids.erase(trk->ids.begin() + off);
    return OMEGA_OK;
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
            auto offset = pos - trk->events.begin();
            if (replacement.tick == tick)
            {
                trk->events[static_cast<size_t>(offset)] = replacement;
            }
            else
            {
                // Move the event (and its id) to the new sorted position so the
                // events/ids vectors stay parallel and identity is preserved.
                const omega_event_id_t id = trk->ids[static_cast<size_t>(offset)];
                trk->events.erase(trk->events.begin() + offset);
                trk->ids.erase(trk->ids.begin() + offset);
                insert_sorted(*trk, replacement, id);
            }
            return OMEGA_OK;
        }
        ++pos;
        ++idx;
    }
    return OMEGA_ERR_NOT_FOUND;
}

omega_status_t TimelineSource::replace_event_by_id(TrackId track_id,
                                                   omega_event_id_t id,
                                                   const Event& replacement)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    const int64_t off = find_id_offset(*trk, id);
    if (off < 0)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    if (replacement.tick == trk->events[static_cast<size_t>(off)].tick)
    {
        trk->events[static_cast<size_t>(off)] = replacement;
    }
    else
    {
        trk->events.erase(trk->events.begin() + off);
        trk->ids.erase(trk->ids.begin() + off);
        insert_sorted(*trk, replacement, id);
    }
    return OMEGA_OK;
}

bool TimelineSource::event_for_id(TrackId track_id, omega_event_id_t id, Event* out) const noexcept
{
    const Track* trk = find_track(track_id);
    if (trk == nullptr || out == nullptr)
    {
        return false;
    }
    const int64_t off = find_id_offset(*trk, id);
    if (off < 0)
    {
        return false;
    }
    *out = trk->events[static_cast<size_t>(off)];
    return true;
}

omega_status_t TimelineSource::copy_events(TrackId track_id,
                                           uint64_t lo,
                                           uint64_t hi,
                                           uint8_t tag_filter,
                                           Event* out_events,
                                           omega_event_id_t* out_ids,
                                           size_t cap,
                                           size_t* out_total) const
{
    if (out_total != nullptr)
    {
        *out_total = 0;
    }
    const Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }

    // Events are tick-sorted; start at the first event with tick >= lo.
    auto begin = std::lower_bound(
        trk->events.begin(), trk->events.end(), lo, [](const Event& ev, uint64_t v) {
            return ev.tick < v;
        });
    size_t total = 0;
    size_t written = 0;
    for (auto it = begin; it != trk->events.end() && it->tick < hi; ++it)
    {
        if (tag_filter != 0xFFu && it->payload_tag != tag_filter)
        {
            continue;
        }
        ++total;
        if (written < cap)
        {
            out_events[written] = *it;
            if (out_ids != nullptr)
            {
                out_ids[written] = trk->ids[static_cast<size_t>(it - trk->events.begin())];
            }
            ++written;
        }
    }
    if (out_total != nullptr)
    {
        *out_total = total;
    }
    return OMEGA_OK;
}

omega_status_t TimelineSource::shift_events(TrackId track_id, int64_t offset_ticks)
{
    Track* trk = find_track(track_id);
    if (trk == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    for (auto& ev : trk->events)
    {
        int64_t new_tick = static_cast<int64_t>(ev.tick) + offset_ticks;
        ev.tick = (new_tick < 0) ? 0u : static_cast<uint64_t>(new_tick);
    }
    // Re-sort once after all ticks are updated, co-sorting ids to stay parallel.
    // Stable by tick so events sharing a tick keep their relative order (and the
    // matching id with each). Mutation thread / engine stopped, so the temporary
    // permutation allocation is acceptable.
    const size_t n = trk->events.size();
    std::vector<uint32_t> perm(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        perm[i] = i;
    }
    std::stable_sort(perm.begin(), perm.end(), [&](uint32_t a, uint32_t b) {
        return trk->events[a].tick < trk->events[b].tick;
    });
    std::pmr::vector<Event> new_events{trk->events.get_allocator()};
    std::pmr::vector<omega_event_id_t> new_ids{trk->ids.get_allocator()};
    new_events.reserve(n);
    new_ids.reserve(n);
    for (uint32_t i : perm)
    {
        new_events.push_back(trk->events[i]);
        new_ids.push_back(trk->ids[i]);
    }
    trk->events = std::move(new_events);
    trk->ids = std::move(new_ids);
    return OMEGA_OK;
}

omega_status_t TimelineSource::swap_tracks(TrackId a, TrackId b)
{
    if (a == b)
    {
        return OMEGA_OK;
    }
    size_t idx_a = tracks_.size();
    size_t idx_b = tracks_.size();
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        if (tracks_[i].id == a)
        {
            idx_a = i;
        }
        else if (tracks_[i].id == b)
        {
            idx_b = i;
        }
    }
    if (idx_a >= tracks_.size() || idx_b >= tracks_.size())
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    std::swap(tracks_[idx_a], tracks_[idx_b]);
    return OMEGA_OK;
}

void TimelineSource::clear_tracks() noexcept
{
    tracks_.clear();
    next_id_ = 1;
    next_event_id_ = 1;
    next_tick_ = 0;
    started_ = false;
    active_notes_.clear();
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
