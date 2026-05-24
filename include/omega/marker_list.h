#pragma once

#include <omega/omega.h>

#include <cstdint>
#include <string>
#include <vector>

namespace omega
{

struct Marker
{
    std::string name;
    uint64_t tick;
};

/*
 * Sorted list of named markers by tick position.
 *
 * Thread: not thread-safe. All access must be from one thread at a time,
 * or externally synchronized. Mutation-thread only during playback.
 */
class MarkerList
{
public:
    /* Insert a marker at tick, maintaining sort order by tick. Duplicate ticks allowed. */
    void add(std::string name, uint64_t tick);

    /*
     * Remove the marker at the given index.
     * Returns OMEGA_ERR_NOT_FOUND if index >= size().
     */
    omega_status_t remove(uint32_t index);

    /*
     * Returns a pointer to the marker at index, or nullptr if out of range.
     */
    [[nodiscard]] const Marker* at(uint32_t index) const noexcept;

    /*
     * Returns the marker at or before tick, or nullptr if none.
     */
    [[nodiscard]] const Marker* find_nearest(uint64_t tick) const noexcept;

    void clear() noexcept;

    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(markers_.size()); }

    [[nodiscard]] const std::vector<Marker>& points() const noexcept { return markers_; }

private:
    std::vector<Marker> markers_;
};

}  // namespace omega
