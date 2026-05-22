#pragma once

#include <omega/clock.h>
#include <omega/commands.h>
#include <omega/detail/spsc_queue.h>
#include <omega/event_source.h>
#include <omega/omega.h>
#include <omega/pattern_library.h>
#include <omega/song_arrangement.h>
#include <omega/tempo_map.h>
#include <omega/timeline.h>
#include <omega/types.h>

#include <atomic>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

namespace omega
{

/*
 * Sequencer engine — the playback machine.
 *
 * Two-thread contract:
 *   Timing thread   — calls process() at whatever interval suits the host.
 *   Mutation thread — calls enqueue() to deliver commands; returns immediately.
 *
 * process() and enqueue() must not be called concurrently from the same side
 * (i.e., two callers may not both call process() simultaneously, nor two
 * callers both call enqueue() simultaneously — SPSC invariant).
 *
 * Add sinks and create tracks from the mutation thread before starting
 * playback. Once playback is running, use enqueue() for all mutations.
 */
class Engine
{
public:
    /*
     * Constructs the engine.
     *
     * clock           — clock source; NULL uses a built-in InternalClock.
     *                   The engine holds a non-owning reference; the clock
     *                   must outlive the engine.
     * mr              — optional PMR allocator; NULL uses the heap default.
     *                   Reserved for future use.
     * queue_capacity  — reserved; current implementation uses a compile-time
     *                   capacity of 4096.
     *
     * Thread: any thread, before first use.
     */
    explicit Engine(ClockSource* clock = nullptr,
                    std::pmr::memory_resource* mr = nullptr,
                    uint32_t queue_capacity = 4096);

    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /* ── Clock ────────────────────────────────────────────────────────────── */

    /*
     * Replaces the active clock source. Call before playback starts.
     * The engine holds a non-owning reference; the clock must outlive the engine.
     *
     * Thread: Mutation thread only, before playback starts.
     */
    void set_clock(ClockSource* clock) noexcept;

    /* ── Sinks ────────────────────────────────────────────────────────────── */

    /*
     * Registers an OutputSink. The engine holds a non-owning reference.
     * Sink must outlive the engine. Call before playback starts.
     *
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns:
     *   OMEGA_OK          — sink registered.
     *   OMEGA_ERR_INVALID — sink is NULL.
     */
    omega_status_t add_sink(OutputSink* sink);

    /* ── Patterns ────────────────────────────────────────────────────────── */

    /*
     * Creates a new pattern in the built-in pattern library.
     * Call before playback starts.
     *
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns the assigned PatternId (always >= 1).
     */
    PatternId create_pattern(std::string name, uint64_t length_ticks);

    /*
     * Removes a pattern from the library. Its ID is never reused.
     * Thread: Mutation thread only, before playback starts.
     */
    void destroy_pattern(PatternId id);

    /*
     * Inserts an event into a pattern in tick-sorted order.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns OMEGA_ERR_NOT_FOUND if id is not a valid pattern.
     */
    omega_status_t pattern_add_event(PatternId id, Event event);

    /*
     * Updates the length of a pattern.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns OMEGA_ERR_NOT_FOUND if id is not a valid pattern.
     */
    omega_status_t pattern_set_length(PatternId id, uint64_t length_ticks);

    /*
     * Returns a non-owning reference to the pattern library.
     * Thread: Mutation thread for writes; Timing thread for reads.
     */
    [[nodiscard]] PatternLibrary& pattern_library() noexcept;
    [[nodiscard]] const PatternLibrary& pattern_library() const noexcept;

    /* ── Song arrangement ────────────────────────────────────────────────── */

    /*
     * Enqueues an entry to append to the song arrangement.
     * Entries are played in order; each pattern repeats repeat_count times.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t song_append(PatternId id, uint32_t repeat_count);

    /*
     * Enqueues a command to clear all arrangement entries and reset playback.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t song_clear();

    /* ── Tracks ───────────────────────────────────────────────────────────── */

    /*
     * Creates a new empty track in the built-in TimelineSource.
     * Call before playback starts.
     *
     * Thread: Mutation thread only, before playback starts.
     */
    TrackId add_track(std::string name);

    /*
     * Sets the OutputSink destination for a track.
     * Call before playback starts.
     *
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t set_track_sink(TrackId track_id, uint32_t sink_id);

    /* ── Command queue ────────────────────────────────────────────────────── */

    /*
     * Enqueue a command for the engine to apply on the next process() call.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK            — command enqueued; will be applied next cycle.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity; command was NOT enqueued.
     */
    omega_status_t enqueue(Command cmd);

    /* ── Process loop ─────────────────────────────────────────────────────── */

    /*
     * Advance the engine by one cycle: drain the command queue, advance the
     * timeline source, and dispatch due events to sinks. Never allocates,
     * blocks, or locks (except within CapturingSink and similar test sinks).
     *
     * Thread: Timing thread only.
     */
    void process();

    /* ── State accessors ──────────────────────────────────────────────────── */

    /*
     * Returns the current transport state. May return a stale value if called
     * concurrently with process().
     *
     * Thread: Any thread.
     */
    [[nodiscard]] TransportState transport_state() const;

    /*
     * Returns the current transport position in nanoseconds from session start.
     * Updated by process(); may return a stale value when read concurrently.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] uint64_t transport_position_ns() const;

private:
    void apply(const AddEventCmd& cmd);
    void apply(const DeleteEventCmd& cmd);
    void apply(const SetTempoCmd& cmd);
    void apply(const TransportCmd& cmd);
    void apply(const SongAppendCmd& cmd);
    void apply(const SongClearCmd& cmd);

    InternalClock internal_clock_;
    ClockSource* clock_;

    PatternLibrary patterns_;

    EventDispatcher::SinkList sinks_;  // sorted by sink_id, non-owning

    TimelineSource timeline_;
    SongArrangementSource song_{patterns_};

    detail::SpscQueue<Command, 4096> queue_;
    TempoMap tempo_map_;
    std::atomic<uint8_t> state_{static_cast<uint8_t>(TransportState::STOPPED)};
    uint64_t session_start_ns_{0};
    std::atomic<uint64_t> last_position_ns_{0};
};

}  // namespace omega
