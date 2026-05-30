#pragma once

#include <omega/omega.h>
#include <omega/pattern.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace omega
{

/*
 * Owns all patterns in the session. Assigns monotonically increasing
 * PatternIds starting at 1. IDs are never reused after destroy().
 *
 * Thread: all mutation methods (create, destroy, add_event, set_length) must
 * be called from the mutation thread before playback starts. During playback,
 * the timing thread reads Pattern* pointers in a read-only fashion — no
 * concurrent writes occur during steady-state playback.
 */
class PatternLibrary
{
public:
    PatternLibrary() = default;
    ~PatternLibrary() = default;

    PatternLibrary(const PatternLibrary&) = delete;
    PatternLibrary& operator=(const PatternLibrary&) = delete;
    PatternLibrary(PatternLibrary&&) = delete;
    PatternLibrary& operator=(PatternLibrary&&) = delete;

    /*
     * Creates a new pattern with the given name and length in ticks.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns the assigned PatternId (always >= 1).
     */
    PatternId create(std::string name, uint64_t length_ticks);

    /*
     * Returns a pointer to the pattern with the given ID, or null if not found.
     * Thread: Mutation thread for mutation; Timing thread for read-only access.
     */
    [[nodiscard]] Pattern* get(PatternId id) noexcept;
    [[nodiscard]] const Pattern* get(PatternId id) const noexcept;

    /*
     * Removes the pattern with the given ID. After this call, get(id) returns
     * null and the ID is never reassigned.
     * Thread: Mutation thread only, before playback starts.
     */
    void destroy(PatternId id);

    /*
     * Inserts an event into a pattern in tick-sorted order.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns:
     *   OMEGA_OK            — event inserted.
     *   OMEGA_ERR_NOT_FOUND — id not found.
     */
    omega_status_t add_event(PatternId id, Event event);

    /*
     * Updates the length of a pattern.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns:
     *   OMEGA_OK            — length updated.
     *   OMEGA_ERR_NOT_FOUND — id not found.
     */
    omega_status_t set_length(PatternId id, uint64_t length_ticks);

    /*
     * Returns the number of live (non-destroyed) patterns in the library.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] uint32_t count() const noexcept;

    /*
     * Invokes fn once per live pattern in unspecified order.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    void for_each(const std::function<void(PatternId, const Pattern&)>& fn) const;

private:
    std::unordered_map<PatternId, std::unique_ptr<Pattern>> patterns_;
    PatternId next_id_{1};
};

}  // namespace omega
