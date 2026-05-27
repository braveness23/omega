#pragma once

#include <omega/clock.h>
#include <omega/commands.h>
#include <omega/detail/spsc_queue.h>
#include <omega/event_anchor_table.h>
#include <omega/event_input.h>
#include <omega/event_source.h>
#include <omega/input_bus.h>
#include <omega/marker_list.h>
#include <omega/modulation_bus.h>
#include <omega/omega.h>
#include <omega/pattern_library.h>
#include <omega/perf_context.h>
#include <omega/perf_slot.h>
#include <omega/region_list.h>
#include <omega/smpte_converter.h>
#include <omega/song_arrangement.h>
#include <omega/tempo_map.h>
#include <omega/time_signature_map.h>
#include <omega/timeline.h>
#include <omega/types.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <optional>
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
class Engine  // NOLINT(clang-analyzer-optin.performance.Padding)
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

    /*
     * Enqueues a command to mute or unmute a specific MIDI channel on a
     * registered sink. channel 0–15 targets one channel; 0xFF targets all
     * channels. When a channel transitions from unmuted to muted, active notes
     * on that channel receive immediate note-off before future note-ons are
     * suppressed. Safe during playback.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — channel > 15 and channel != 0xFF.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t sink_set_mute(uint32_t sink_id, uint8_t channel, bool muted);

    /*
     * Enqueues a command to solo or un-solo a specific MIDI channel on a
     * registered sink. channel 0–15 targets one channel; 0xFF targets all
     * channels. While any channel is soloed, only soloed channels produce
     * output; all others are effectively muted. Active notes on channels that
     * become suppressed by a new solo receive immediate note-offs. Safe during
     * playback.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — channel > 15 and channel != 0xFF.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t sink_set_solo(uint32_t sink_id, uint8_t channel, bool soloed);

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
     * Enqueues a command to replace the event at event_index (0-based) in the
     * given pattern with replacement. If the replacement tick differs from the
     * original, the event vector is re-sorted. Safe during playback.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t pattern_replace_event(PatternId id,
                                         uint32_t event_index,
                                         const Event& replacement);

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

    /* ── Performance source ──────────────────────────────────────────────── */

    /*
     * Assigns a pattern to a performance slot.
     * pattern == 0 unassigns (any state → EMPTY).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_assign(SlotId slot, PatternId pattern);

    /*
     * Cues the assigned pattern for the slot.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_cue(SlotId slot, CueMode mode);

    /*
     * Stops the slot at the given cue mode boundary.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_stop(SlotId slot, CueMode mode);

    /*
     * Stops all slots.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_stop_all(CueMode mode);

    /*
     * Sets per-slot transpose in semitones (-24 to +24).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_set_transpose(SlotId slot, int8_t semitones);

    /*
     * Sets per-slot velocity scale (0–200, 100 = unity).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_set_velocity_scale(SlotId slot, uint8_t scale);

    /*
     * Sets per-slot random bias (0–100).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t perf_set_random_bias(SlotId slot, uint8_t bias);

    /* ── Inputs ──────────────────────────────────────────────────────────────── */

    /*
     * Enqueues a command to register an EventInput. On the next process() call,
     * the input is added to the polling list.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — input is NULL.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t add_input(EventInput* input);

    /*
     * Enqueues a command to deregister an EventInput. On the next process() call,
     * the input is removed from the polling list.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — input is NULL.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t remove_input(EventInput* input);

    /*
     * Returns the cumulative number of events dropped due to InputBus overflow.
     * Never resets; monotonically increasing.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] uint32_t input_overflow_count() const noexcept;

    /* ── Modulation bus ──────────────────────────────────────────────────────── */

    /*
     * Returns the engine's ModulationBus for direct C++ access.
     * Thread: Mutation thread for register_channel/find; Timing thread for get/set.
     */
    [[nodiscard]] ModulationBus& modulation_bus() noexcept { return mod_bus_; }

    /* ── Performance context ─────────────────────────────────────────────────── */

    /*
     * Enqueues a command to set the active scale.
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_scale(const omega_scale_t& scale);

    /*
     * Enqueues a command to set the active chord.
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_chord(const omega_chord_t& chord);

    /*
     * Enqueues a command to set the global transpose (-24 to +24 semitones).
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_transpose(int8_t semitones);

    /*
     * Enqueues a command to set the global velocity scale (0-200, 100 = unity).
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_velocity(uint8_t velocity);

    /*
     * Enqueues a command to set the chaos level (0-100).
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_chaos(uint8_t chaos);

    /*
     * Enqueues a command to set the groove template and swing.
     * Thread: Mutation thread only.
     */
    omega_status_t ctx_set_groove(uint8_t groove_id, float swing);

    /*
     * Returns a snapshot of the current performance context.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    void ctx_get(omega_perf_ctx_t& out) const noexcept { out = perf_ctx_; }

    /* ── Custom sources ──────────────────────────────────────────────────────── */

    /*
     * Enqueues a command to register a custom EventSource.
     * The source is called from process() in priority/registration order,
     * before the built-in sources (which are always priority PLAYBACK).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — source is NULL.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t add_source(EventSource* source, uint32_t priority);

    /*
     * Enqueues a command to deregister a custom EventSource.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — source is NULL.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t remove_source(EventSource* source);

    /* ── Tempo map ──────────────────────────────────────────────────────────── */

    /*
     * Insert or replace a tempo point at tick.
     * bpm_milli = BPM × 1000 (e.g. 120 BPM = 120000). Zero is invalid.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — bpm_milli is zero.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t tempo_set(uint64_t tick, uint32_t bpm_milli);

    /*
     * Remove the tempo point at exactly tick.
     * The default point at tick 0 cannot be removed; a replacement is inserted
     * at tick 0 with 120 BPM if tick == 0.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t tempo_remove(uint64_t tick);

    /* ── Time signature map ─────────────────────────────────────────────────── */

    /*
     * Insert or replace a time signature at tick.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — denominator is not a power of 2 in [1,32] or numerator is zero.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t timesig_set(uint64_t tick, uint8_t numerator, uint8_t denominator);

    /*
     * Remove the time signature at exactly tick.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t timesig_remove(uint64_t tick);

    /*
     * Clear all time signature entries (enter freeform mode).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t timesig_clear();

    /*
     * Returns a non-owning reference to the TimeSignatureMap.
     * Thread: Timing thread for reads from advance(); Mutation thread otherwise.
     */
    [[nodiscard]] const TimeSignatureMap& timesig_map() const noexcept { return timesig_map_; }
    [[nodiscard]] TimeSignatureMap& timesig_map() noexcept { return timesig_map_; }
    [[nodiscard]] const TempoMap& tempo_map() const noexcept { return tempo_map_; }
    [[nodiscard]] TempoMap& tempo_map() noexcept { return tempo_map_; }

    /* Raw access to the built-in timeline source for serialization (mutation thread only). */
    [[nodiscard]] const TimelineSource& timeline_source() const noexcept { return timeline_; }

    /* ── Markers and regions ─────────────────────────────────────────────────── */

    /*
     * Returns the session marker list.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] MarkerList& marker_list() noexcept { return marker_list_; }
    [[nodiscard]] const MarkerList& marker_list() const noexcept { return marker_list_; }

    /*
     * Returns the session region list.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] RegionList& region_list() noexcept { return region_list_; }
    [[nodiscard]] const RegionList& region_list() const noexcept { return region_list_; }

    /*
     * Returns the session event anchor table.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] EventAnchorTable& event_anchors() noexcept { return event_anchors_; }
    [[nodiscard]] const EventAnchorTable& event_anchors() const noexcept { return event_anchors_; }

    /*
     * Inserts an event directly into a timeline track, bypassing the command queue.
     * Only safe when the engine is stopped (e.g., during SMF import).
     *
     * Thread: Mutation thread only, engine must be stopped.
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t add_track_event(TrackId track_id, const Event& event);

    /* ── Loop region ─────────────────────────────────────────────────────────── */

    /* Snapshot of the transport loop region (returned by loop_region()). */
    struct LoopRegion
    {
        uint64_t start_tick;
        uint64_t end_tick;
        bool enabled;
    };

    /*
     * Enqueues a command to set the transport loop region and enable looping.
     * While playing, when the transport position reaches end_tick it automatically
     * locates back to start_tick (sending note-offs and resetting source cursors).
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — end_tick <= start_tick.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t loop_set(uint64_t start_tick, uint64_t end_tick);

    /*
     * Enqueues a command to disable looping and clear the loop region.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t loop_clear();

    /*
     * Enqueues a command to enable or disable looping without changing the
     * stored loop region.
     *
     * Thread: Mutation thread only. Must not be called concurrently with
     * process() — reads loop_start_tick_ and loop_end_tick_.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t loop_enable(bool enabled);

    /*
     * Returns a snapshot of the current loop region (start, end, enabled).
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] LoopRegion loop_region() const noexcept
    {
        return {loop_start_tick_, loop_end_tick_, loop_enabled_};
    }

    /* ── SMPTE config ────────────────────────────────────────────────────────── */

    /*
     * Set the SMPTE frame-rate config.
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_INVALID    — config is not valid (see is_valid_smpte_config).
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t smpte_config_set(const SmpteConfig& config);

    /*
     * Clear the SMPTE config (SmpteConverter calls will return NO_SMPTE_CONFIG).
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK             — command enqueued.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
     */
    omega_status_t smpte_config_clear();

    /*
     * Returns a copy of the current SMPTE config, if set.
     * Thread: Mutation thread only. Must not be called concurrently with process().
     */
    [[nodiscard]] const std::optional<SmpteConfig>& smpte_config() const noexcept
    {
        return smpte_config_;
    }

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

    /*
     * Returns the current transport position in ticks from session start.
     * Converts the stored nanosecond position through the TempoMap.
     * Updated by process(); may return a stale value when read concurrently.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] uint64_t transport_position_tick() const;

    /*
     * Returns a position snapshot updated at the end of each process() call.
     * Each field is individually atomic. In rare cases, fields from two
     * adjacent cycles may appear together; this is imperceptible at normal
     * display refresh rates.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] omega_position_t position() const noexcept;

    /*
     * Returns the current state of the given performance slot.
     * Returns SlotState::EMPTY for out-of-range slot indices.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] SlotState perf_slot_state(uint32_t slot) const noexcept;

    /*
     * Returns true if the given MIDI channel on the specified sink is muted.
     * Returns false for unregistered sink_id or channel > 15.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] bool sink_is_muted(uint32_t sink_id, uint8_t channel) const noexcept;

    /*
     * Returns true if the given MIDI channel on the specified sink is soloed.
     * Returns false for unregistered sink_id or channel > 15.
     *
     * Thread: Any thread.
     */
    [[nodiscard]] bool sink_is_soloed(uint32_t sink_id, uint8_t channel) const noexcept;

    /*
     * Mute/solo state for one registered sink.
     * Maintained in sync with sinks_: one entry per sink, in the same order.
     * Timing-thread-owned after the first process() call.
     *
     * This struct is public so that FilteringDispatcher (defined in engine.cpp)
     * can reference it without a friend declaration. Do not use it directly;
     * it is an engine implementation detail.
     */
    struct SinkFilterState
    {
        uint32_t sink_id{0};
        OutputSink* ptr{nullptr};  // non-owning; used for direct note-off flush

        std::atomic<uint16_t> muted{0};   // bit N = MIDI channel N is muted
        std::atomic<uint16_t> soloed{0};  // bit N = MIDI channel N is soloed

        // Active note bitmask: 128 bits per channel, packed as 16 bytes.
        // Bit (note & 7) of active_notes[channel][note >> 3] is set while
        // a NOTE_ON has been dispatched and the matching NOTE_OFF has not yet
        // been dispatched. Used to flush note-offs when muting mid-note.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        uint8_t active_notes[16][16]{};

        SinkFilterState() = default;
        SinkFilterState(SinkFilterState&& o) noexcept
            : sink_id{o.sink_id},
              ptr{o.ptr},
              muted{o.muted.load(std::memory_order_relaxed)},
              soloed{o.soloed.load(std::memory_order_relaxed)}
        {
            std::memcpy(active_notes, o.active_notes, sizeof(active_notes));
        }
        SinkFilterState& operator=(SinkFilterState&& o) noexcept
        {
            sink_id = o.sink_id;
            ptr = o.ptr;
            muted.store(o.muted.load(std::memory_order_relaxed), std::memory_order_relaxed);
            soloed.store(o.soloed.load(std::memory_order_relaxed), std::memory_order_relaxed);
            std::memcpy(active_notes, o.active_notes, sizeof(active_notes));
            return *this;
        }
        SinkFilterState(const SinkFilterState&) = delete;
        SinkFilterState& operator=(const SinkFilterState&) = delete;
    };

private:
    void apply(const AddEventCmd& cmd);
    void apply(const DeleteEventCmd& cmd);
    void apply(const ReplaceEventCmd& cmd);
    void apply(const SetTempoCmd& cmd);
    void apply(const SetTempoPointCmd& cmd);
    void apply(const RemoveTempoPointCmd& cmd);
    void apply(const SetLoopCmd& cmd);
    void apply(const TransportCmd& cmd);
    void apply(const SongAppendCmd& cmd);
    void apply(const SongClearCmd& cmd);
    void apply(const PerfAssignCmd& cmd);
    void apply(const PerfCueCmd& cmd);
    void apply(const PerfStopCmd& cmd);
    void apply(const PerfStopAllCmd& cmd);
    void apply(const PerfSetTransposeCmd& cmd);
    void apply(const PerfSetVelocityScaleCmd& cmd);
    void apply(const PerfSetRandomBiasCmd& cmd);
    void apply(const AddInputCmd& cmd);
    void apply(const RemoveInputCmd& cmd);
    void apply(const SetCtxScaleCmd& cmd);
    void apply(const SetCtxChordCmd& cmd);
    void apply(const SetCtxTransposeCmd& cmd);
    void apply(const SetCtxVelocityCmd& cmd);
    void apply(const SetCtxChaosCmd& cmd);
    void apply(const SetCtxGrooveCmd& cmd);
    void apply(const AddSourceCmd& cmd);
    void apply(const RemoveSourceCmd& cmd);
    void apply(const SetTimeSigCmd& cmd);
    void apply(const RemoveTimeSigCmd& cmd);
    void apply(const ClearTimeSigCmd& cmd);
    void apply(const SetSmpteConfigCmd& cmd);
    void apply(const ClearSmpteConfigCmd& cmd);
    void apply(const SetSinkMuteCmd& cmd);
    void apply(const SetSinkSoloCmd& cmd);

    /*
     * Flushes active notes for a given channel mask.
     * ch_mask: bit N = flush channel N. Pass 0xFFFFu to flush all channels.
     * Sends NOTE_OFF directly to f.ptr, which bypasses the FilteringDispatcher.
     * Called from the timing thread during command application (before the
     * per-cycle EventDispatcher is created).
     */
    void flush_active_notes(SinkFilterState& f, uint16_t ch_mask) noexcept;

    InternalClock internal_clock_;
    ClockSource* clock_;

    PatternLibrary patterns_;

    EventDispatcher::SinkList sinks_;  // sorted by sink_id, non-owning

    // Mute/solo state — one entry per registered sink, parallel to sinks_.
    // Modified from timing thread only (via command queue or add_sink()).
    std::vector<SinkFilterState> sink_filters_;

    // True when any channel on any sink has its solo bit set.
    // Timing-thread-owned; recomputed by apply(SetSinkSoloCmd).
    std::atomic<bool> any_soloed_{false};

    // timesig_map_ must be declared before perf_ — perf_ holds a const reference.
    TimeSignatureMap timesig_map_;

    TimelineSource timeline_;
    SongArrangementSource song_{patterns_};
    PerformanceSource perf_{patterns_, timesig_map_};

    std::vector<EventInput*> inputs_;  // non-owning; modified only from timing thread via queue
    InputBus input_bus_;

    ModulationBus mod_bus_;

    // perf_ctx_: timing-thread-only; snapshotted into ProcessContext each cycle.
    // omega_ctx_get() reads this from mutation thread — must not be called
    // concurrently with process().
    omega_perf_ctx_t perf_ctx_{};

    // custom_sources_: non-owning; sorted by (priority, registration order).
    // Modified only from timing thread via command queue.
    // Each entry: {priority, source*}
    std::vector<std::pair<uint32_t, EventSource*>> custom_sources_;

    std::optional<SmpteConfig> smpte_config_;

    MarkerList marker_list_;
    RegionList region_list_;
    EventAnchorTable event_anchors_;

    detail::SpscQueue<Command, 4096> queue_;
    TempoMap tempo_map_;
    std::atomic<uint8_t> state_{static_cast<uint8_t>(TransportState::STOPPED)};
    uint64_t session_start_ns_{0};
    std::atomic<uint64_t> last_position_ns_{0};

    // Loop region — timing-thread-owned; read from mutation thread only when
    // process() is not running (same contract as smpte_config_ and perf_ctx_).
    uint64_t loop_start_tick_{0};
    uint64_t loop_end_tick_{0};
    bool loop_enabled_{false};

    // Loop wrap counter — incremented each time the loop region wraps;
    // reset to 0 whenever the loop region itself changes. Timing-thread-only
    // (only modified/read inside process() and apply(SetLoopCmd)).
    uint64_t loop_count_{0};

    // Position snapshot — five individually-atomic fields written by the
    // timing thread at the end of each process() cycle. Any thread may read
    // them. Each field is lock-free on all target platforms. In rare cases,
    // a reader may observe fields from two adjacent cycles; this is
    // imperceptible at normal display refresh rates.
    std::atomic<uint32_t> snap_bar_{0};
    std::atomic<uint8_t> snap_beat_{0};
    std::atomic<uint32_t> snap_sub_{0};
    std::atomic<uint64_t> snap_loop_count_{0};
    std::atomic<uint64_t> snap_tick_{0};
};

}  // namespace omega
