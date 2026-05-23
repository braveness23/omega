#include <omega/song_arrangement.h>

#include <algorithm>
#include <cstring>

namespace omega
{

SongArrangementSource::SongArrangementSource(const PatternLibrary& library) noexcept
    : library_{library}
{}

void SongArrangementSource::append(PatternId pattern_id, uint32_t repeat_count)
{
    entries_.push_back({pattern_id, repeat_count});
}

void SongArrangementSource::clear()
{
    entries_.clear();
    active_notes_.clear();
    next_tick_ = 0;
    cur_entry_ = 0;
    cur_rep_ = 0;
    iter_start_tick_ = 0;
}

void SongArrangementSource::advance(uint64_t to_tick,
                                    EventDispatcher& dispatcher,
                                    ProcessContext& /*ctx*/)
{
    uint64_t cur_from = next_tick_;

    while (cur_entry_ < static_cast<uint32_t>(entries_.size()) && cur_from <= to_tick)
    {
        const ArrangementEntry& entry = entries_[cur_entry_];

        if (entry.repeat_count == 0)
        {
            ++cur_entry_;
            cur_rep_ = 0;
            continue;
        }

        const Pattern* pat = library_.get(entry.pattern_id);
        if (pat == nullptr || pat->length_ticks == 0)
        {
            ++cur_entry_;
            cur_rep_ = 0;
            continue;
        }

        uint64_t iter_end_tick = iter_start_tick_ + pat->length_ticks;

        if (cur_from >= iter_end_tick)
        {
            // cur_from has moved past this iteration (e.g. large clock jump).
            ++cur_rep_;
            iter_start_tick_ = iter_end_tick;
            if (cur_rep_ >= entry.repeat_count)
            {
                cur_rep_ = 0;
                ++cur_entry_;
            }
            continue;
        }

        // Dispatch events in this iteration that fall within [cur_from, to_tick].
        uint64_t window_end = std::min(to_tick, iter_end_tick - 1u);
        uint64_t pat_from = cur_from - iter_start_tick_;
        uint64_t pat_to = window_end - iter_start_tick_;

        auto pos = std::lower_bound(
            pat->events.begin(), pat->events.end(), pat_from, [](const Event& ev, uint64_t v) {
                return ev.tick < v;
            });

        while (pos != pat->events.end() && pos->tick <= pat_to)
        {
            Event ev = *pos;
            ev.tick = iter_start_tick_ + pos->tick;
            dispatcher.dispatch(ev);

            if (pos->payload_tag == OMEGA_NOTE_ON)
            {
                uint32_t duration = 0;
                std::memcpy(&duration, &pos->data[2], sizeof(duration));
                if (duration > 0)
                {
                    active_notes_.push_back(
                        {ev.tick + duration, ev.sink_id, ev.data[0], ev.channel});
                }
            }
            ++pos;
        }

        cur_from = window_end + 1;

        if (window_end == iter_end_tick - 1u)
        {
            // Consumed through the end of this iteration — advance to the next.
            ++cur_rep_;
            iter_start_tick_ = iter_end_tick;
            if (cur_rep_ >= entry.repeat_count)
            {
                cur_rep_ = 0;
                ++cur_entry_;
            }
        }
    }

    fire_note_offs(to_tick, dispatcher);
    next_tick_ = to_tick + 1;
}

void SongArrangementSource::on_locate(uint64_t tick,
                                      EventDispatcher& /*dispatcher*/,
                                      ProcessContext& /*ctx*/)
{
    active_notes_.clear();
    next_tick_ = tick;

    auto n = static_cast<uint32_t>(entries_.size());
    uint64_t abs_start = 0;

    for (uint32_t i = 0; i < n; ++i)
    {
        const ArrangementEntry& entry = entries_[i];

        if (entry.repeat_count == 0)
        {
            continue;
        }

        const Pattern* pat = library_.get(entry.pattern_id);
        if (pat == nullptr || pat->length_ticks == 0)
        {
            continue;
        }

        uint64_t entry_total = pat->length_ticks * static_cast<uint64_t>(entry.repeat_count);

        if (tick < abs_start + entry_total)
        {
            cur_entry_ = i;
            uint64_t offset = tick - abs_start;
            cur_rep_ = static_cast<uint32_t>(offset / pat->length_ticks);
            iter_start_tick_ = abs_start + static_cast<uint64_t>(cur_rep_) * pat->length_ticks;
            return;
        }

        abs_start += entry_total;
    }

    // tick is at or past the end of the arrangement.
    cur_entry_ = n;
    cur_rep_ = 0;
    iter_start_tick_ = abs_start;
}

void SongArrangementSource::fire_note_offs(uint64_t to_tick, EventDispatcher& dispatcher)
{
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
            off.data[1] = 64;
            dispatcher.dispatch(off);
            it = active_notes_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}  // namespace omega
