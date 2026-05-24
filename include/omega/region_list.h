#pragma once

#include <omega/omega.h>

#include <cstdint>
#include <string>
#include <vector>

namespace omega
{

enum class RegionType : uint8_t
{
    LOOP = 0,
    PUNCH = 1,
    SECTION = 2,
};

struct Region
{
    std::string name;
    uint64_t start_tick;
    uint64_t end_tick;
    RegionType type;
};

/*
 * Sorted list of named regions by start_tick.
 *
 * Thread: not thread-safe. Mutation-thread only during playback.
 */
class RegionList
{
public:
    /*
     * Add a region [start, end). Validates start < end.
     * Returns OMEGA_ERR_INVALID if start >= end.
     * Maintains sort order by start_tick.
     */
    omega_status_t add(std::string name, uint64_t start, uint64_t end, RegionType type);

    /*
     * Remove the region at the given index.
     * Returns OMEGA_ERR_NOT_FOUND if index >= size().
     */
    omega_status_t remove(uint32_t index);

    /*
     * Returns a pointer to the region at index, or nullptr if out of range.
     */
    [[nodiscard]] const Region* at(uint32_t index) const noexcept;

    /*
     * Returns the first region containing tick (start_tick <= tick < end_tick),
     * or nullptr if none. For overlapping regions, returns the one with the
     * lowest start_tick.
     */
    [[nodiscard]] const Region* find_containing(uint64_t tick) const noexcept;

    void clear() noexcept;

    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(regions_.size()); }

    [[nodiscard]] const std::vector<Region>& points() const noexcept { return regions_; }

private:
    std::vector<Region> regions_;
};

}  // namespace omega
