#pragma once

#include <omega/event_source.h>
#include <omega/track.h>

#include <cstdint>
#include <string>
#include <vector>

namespace omega
{

/*
 * Built-in linear multi-track playback source.
 *
 * Owns a collection of tracks. On each advance() cycle, iterates all tracks
 * and dispatches events that fall within the (prev_tick, to_tick] window.
 * Note-offs are tracked separately in an active-notes table and fired when
 * their deadline tick is reached.
 *
 * Mutation methods (add_track, set_sink, add_event, remove_event) must be
 * called from the mutation thread. In the engine, add_event and remove_event
 * are applied via the command queue inside process() before advance() runs,
 * so they are safe. add_track and set_sink are called before playback starts.
 *
 * Thread: advance() and on_locate() from the timing thread only.
 */
class TimelineSource final : public EventSource
{
public:
    /*
     * Creates a new empty track and returns its TrackId.
     * Thread: Mutation thread only, before playback starts.
     */
    TrackId add_track(std::string name);

    /*
     * Sets the OutputSink destination for a track.
     * Thread: Mutation thread only, before playback starts.
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t set_sink(TrackId track_id, uint32_t sink_id);

    /*
     * Sets the representative MIDI channel for a track (display/metadata only;
     * playback routes each event by its own channel). Thread: Mutation thread
     * only, before playback starts.
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t set_channel(TrackId track_id, uint8_t channel);

    /*
     * Replaces a track's name. Safe during playback: advance() never reads
     * Track::name, so the only constraint is the usual single-mutation-thread
     * rule. Thread: Mutation thread only.
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t set_name(TrackId track_id, std::string name);

    /*
     * Per-track mute/solo. While any track is soloed, only soloed tracks play.
     * A muted (or solo-suppressed) track dispatches no new events; notes it has
     * already started with an inline duration still release at their scheduled
     * note-off (no stuck notes, no abrupt cut). Thread: Timing thread only
     * (applied from the engine command queue).
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t set_track_mute(TrackId track_id, bool muted);
    omega_status_t set_track_solo(TrackId track_id, bool soloed);

    /*
     * Per-track mute/solo queries. Return false for an unregistered track_id.
     * Thread: Any thread (may return a stale value if read concurrently with a
     * mute/solo change being applied on the timing thread).
     */
    [[nodiscard]] bool track_is_muted(TrackId track_id) const noexcept;
    [[nodiscard]] bool track_is_soloed(TrackId track_id) const noexcept;

    /*
     * Inserts an event in tick-sorted order.
     * Thread: Timing thread only (called from engine command queue drain).
     *
     * Returns OMEGA_ERR_NOT_FOUND if track_id is not registered.
     */
    omega_status_t add_event(TrackId track_id, const Event& event);

    /*
     * Removes the event at (tick, index) within that tick's run.
     * Thread: Timing thread only (called from engine command queue drain).
     *
     * Returns OMEGA_ERR_NOT_FOUND if the event does not exist.
     */
    omega_status_t remove_event(TrackId track_id, uint64_t tick, uint32_t index);

    /*
     * Replaces the event at (tick, index) with replacement.
     * If replacement.tick != tick, the event vector is re-sorted after the
     * replacement so the timeline remains ordered.
     * Thread: Timing thread only (called from engine command queue drain).
     *
     * Returns OMEGA_ERR_NOT_FOUND if the event does not exist.
     */
    omega_status_t replace_event(TrackId track_id,
                                 uint64_t tick,
                                 uint32_t index,
                                 const Event& replacement);

    /*
     * Dispatches all events in (next_tick_, to_tick] across all non-muted
     * tracks, then fires any scheduled note-offs.
     *
     * Thread: Timing thread only. Never allocates during steady-state playback
     * unless a note-on is dispatched (adds to active_notes_).
     */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

    /*
     * Resets the dispatch position to `tick` and clears active notes.
     * Thread: Timing thread only.
     */
    void on_locate(uint64_t tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

    /* Raw access for serialization (mutation thread only, engine stopped). */
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }

private:
    struct ActiveNote
    {
        uint64_t off_tick;
        uint32_t sink_id;
        uint8_t note;
        uint8_t channel;
    };

    [[nodiscard]] Track* find_track(TrackId id) noexcept;
    [[nodiscard]] const Track* find_track(TrackId id) const noexcept;

    std::vector<Track> tracks_;
    TrackId next_id_{1};

    uint64_t next_tick_{0};  // first tick to dispatch on next advance()
    bool started_{false};    // false until first advance() call

    std::vector<ActiveNote> active_notes_;
};

}  // namespace omega
