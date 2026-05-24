#pragma once

#include <omega/omega.h>

#include <cstdint>
#include <string>
#include <vector>

namespace omega
{

/* AnchorPoint flag constants. */
constexpr uint32_t ANCHOR_SNAP = 0x01u; /* used for snap-to-grid operations */
constexpr uint32_t ANCHOR_WARP = 0x02u; /* stretch/warp boundary */
constexpr uint32_t ANCHOR_CUE = 0x04u;  /* cue launch point */

struct AnchorPoint
{
    std::string name;
    uint64_t offset_ticks;
    uint32_t flags;
};

/*
 * Sorted list of named anchor points by offset_ticks.
 *
 * An AnchorList travels with its owner (a Pattern or an EventAnchorTable
 * entry). It is mutation-thread-only during playback.
 *
 * Thread: not thread-safe. All access must be from one thread at a time.
 */
class AnchorList
{
public:
    /* Insert an anchor, maintaining sort order by offset_ticks. */
    void add(std::string name, uint64_t offset_ticks, uint32_t flags);

    /*
     * Remove the anchor with the given name (first match).
     * Returns OMEGA_ERR_NOT_FOUND if not found.
     * Updates the active-snap index if the removed anchor was the active snap.
     */
    omega_status_t remove(const std::string& name);

    /*
     * Returns a pointer to the anchor at index, or nullptr if out of range.
     */
    [[nodiscard]] const AnchorPoint* at(uint32_t index) const noexcept;

    /*
     * Returns a pointer to the first anchor with the given name, or nullptr.
     */
    [[nodiscard]] const AnchorPoint* find_by_name(const std::string& name) const noexcept;

    /*
     * Returns pointers to all anchors that have the ANCHOR_SNAP flag set.
     */
    [[nodiscard]] std::vector<const AnchorPoint*> snap_anchors() const;

    /*
     * Set the active snap anchor by index. The anchor at index must have the
     * ANCHOR_SNAP flag set; otherwise returns OMEGA_ERR_INVALID.
     * Returns OMEGA_ERR_NOT_FOUND if index >= size().
     */
    omega_status_t set_active_snap(uint32_t index);

    /*
     * Returns the currently active snap anchor, or nullptr if none is set.
     */
    [[nodiscard]] const AnchorPoint* active_snap() const noexcept;

    void clear() noexcept;

    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(anchors_.size()); }

    [[nodiscard]] const std::vector<AnchorPoint>& points() const noexcept { return anchors_; }

private:
    std::vector<AnchorPoint> anchors_;
    int32_t active_snap_index_{-1};
};

}  // namespace omega
