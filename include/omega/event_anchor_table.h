#pragma once

#include <omega/anchor_point.h>

#include <cstdint>
#include <unordered_map>

namespace omega
{

/*
 * Sparse side table that associates AnchorLists with individual events.
 *
 * Keys are formed as (container_id << 32) | event_index, where container_id
 * is a track or pattern ID and event_index is the event's position in the
 * owning vector. Events without anchors have no entry (zero overhead).
 *
 * Thread: not thread-safe. Mutation-thread only during playback.
 */
class EventAnchorTable
{
public:
    /*
     * Returns the AnchorList for (container_id, event_index), creating an
     * empty one if it does not exist.
     */
    AnchorList& get_or_create(uint32_t container_id, uint32_t event_index);

    /*
     * Returns a pointer to the AnchorList for (container_id, event_index),
     * or nullptr if no entry exists.
     */
    [[nodiscard]] AnchorList* get(uint32_t container_id, uint32_t event_index) noexcept;
    [[nodiscard]] const AnchorList* get(uint32_t container_id, uint32_t event_index) const noexcept;

    /*
     * Removes the entry for (container_id, event_index).
     * Returns OMEGA_ERR_NOT_FOUND if no entry exists.
     */
    omega_status_t remove(uint32_t container_id, uint32_t event_index);

    void clear() noexcept;

    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(table_.size()); }

private:
    static uint64_t key(uint32_t container_id, uint32_t event_index) noexcept
    {
        return (static_cast<uint64_t>(container_id) << 32) | static_cast<uint64_t>(event_index);
    }

    std::unordered_map<uint64_t, AnchorList> table_;
};

}  // namespace omega
