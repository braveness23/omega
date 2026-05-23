#pragma once

#include <omega/event_source.h>
#include <omega/pattern_library.h>
#include <omega/types.h>

#include <cstdint>
#include <vector>

namespace omega
{

struct ArrangementEntry
{
    PatternId pattern_id;
    uint32_t repeat_count;
};

/*
 * Built-in song-arrangement playback source.
 *
 * Plays the entries in sequence; each entry's pattern repeats repeat_count
 * times before advancing to the next entry. Entries with repeat_count == 0 or
 * an unknown pattern_id are silently skipped (they contribute zero ticks).
 *
 * The source reads patterns from a PatternLibrary supplied at construction.
 * The library must outlive this source.
 *
 * Thread: advance() and on_locate() are called from the timing thread only.
 *         append() and clear() are applied from within the command-queue drain
 *         (also timing thread), so no locking is required.
 */
class SongArrangementSource final : public EventSource
{
public:
    explicit SongArrangementSource(const PatternLibrary& library) noexcept;

    /*
     * Appends one entry to the arrangement.
     * Thread: Timing thread only (applied from command queue).
     */
    void append(PatternId pattern_id, uint32_t repeat_count);

    /*
     * Removes all entries and resets playback state to the beginning.
     * Thread: Timing thread only (applied from command queue).
     */
    void clear();

    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    struct ActiveNote
    {
        uint64_t off_tick;
        uint32_t sink_id;
        uint8_t note;
        uint8_t channel;
    };

    void fire_note_offs(uint64_t to_tick, EventDispatcher& dispatcher);

    const PatternLibrary& library_;
    std::vector<ArrangementEntry> entries_;

    uint64_t next_tick_{0};

    // Current playback cursor.
    uint32_t cur_entry_{0};
    uint32_t cur_rep_{0};
    uint64_t iter_start_tick_{0};  // absolute tick where current iteration began

    std::vector<ActiveNote> active_notes_;
};

}  // namespace omega
