#pragma once

#include <omega/types.h>

#include <cstdint>
#include <memory_resource>
#include <string>

namespace omega
{

/*
 * A named, length-bounded sequence of events.
 *
 * Events are stored in tick-sorted order within a PMR vector. length_ticks
 * defines the loop boundary used by SongArrangementSource and
 * PerformanceSource: events at tick >= length_ticks are outside the pattern.
 *
 * Thread: not thread-safe. Mutations are from the mutation thread only
 * (before playback starts). During playback the timing thread reads events
 * in a read-only fashion; no concurrent writes occur.
 */
struct Pattern
{
    PatternId id{0};
    std::string name;
    uint64_t length_ticks{0};
    std::pmr::vector<Event> events;

    Pattern(PatternId id_,
            std::string name_,
            uint64_t length_ticks_,
            std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : id{id_}, name{std::move(name_)}, length_ticks{length_ticks_}, events{mr}
    {}

    ~Pattern() = default;
    Pattern(const Pattern&) = delete;
    Pattern& operator=(const Pattern&) = delete;
    Pattern(Pattern&&) = default;
    Pattern& operator=(Pattern&&) = default;
};

}  // namespace omega
