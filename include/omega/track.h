#pragma once

#include <omega/types.h>

#include <memory_resource>
#include <string>

namespace omega
{

/*
 * A single playback track: an ordered (by tick) sequence of events routed
 * to a specific OutputSink + MIDI channel.
 *
 * Owned by TimelineSource. Not thread-safe — all mutations must go through
 * the engine command queue and are applied from the timing thread.
 */
struct Track
{
    TrackId id;
    std::string name;
    std::pmr::vector<Event> events;
    uint32_t sink_id;
    uint8_t channel;
    bool muted;

    Track(TrackId id_, std::string name_,
          std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : id{id_}
        , name{std::move(name_)}
        , events{mr}
        , sink_id{0}
        , channel{0}
        , muted{false}
    {
    }

    Track(const Track&) = delete;
    Track& operator=(const Track&) = delete;
    Track(Track&&) = default;
    Track& operator=(Track&&) = default;
};

}  // namespace omega
