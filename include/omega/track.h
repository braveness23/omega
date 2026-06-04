#pragma once

#include <omega/meta_event.h>
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
    TrackId id{0};
    std::string name;
    std::pmr::vector<Event> events;
    /*
     * Stable per-event identity, parallel to `events`: ids[i] is the opaque,
     * never-reused id of events[i]. Maintained in lockstep with `events` by all
     * TimelineSource mutators (insert/remove/replace/shift) so an id always
     * names the same musical event even after the vector is re-sorted by a
     * tick-changing edit. The Event struct itself is a fixed 24-byte ABI/SMF
     * layout with no room for an id, hence this side table.
     */
    std::pmr::vector<omega_event_id_t> ids;
    /*
     * SMF text-class meta events (text, instrument name, lyric, …) preserved for
     * round-trip fidelity. Never read by the timing thread — purely descriptive
     * metadata carried alongside the musical events.
     */
    std::pmr::vector<MetaEvent> meta;
    uint32_t sink_id{0};
    uint8_t channel{0};
    bool muted{false};
    bool soloed{false};

    Track(TrackId id_,
          std::string name_,
          std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : id{id_}, name{std::move(name_)}, events{mr}, ids{mr}, meta{mr}
    {}

    ~Track() = default;

    Track(const Track&) = delete;
    Track& operator=(const Track&) = delete;
    Track(Track&&) = default;
    Track& operator=(Track&&) = default;
};

}  // namespace omega
