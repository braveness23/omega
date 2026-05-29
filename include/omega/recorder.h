#pragma once

#include <omega/event_source.h>
#include <omega/export.h>
#include <omega/types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace omega
{

class TimelineSource;

/*
 * Records live MIDI input into a timeline track.
 *
 * Recorder is a custom EventSource. Register it with the engine at priority
 * OMEGA_SOURCE_PRIORITY_MODULATOR (0) so it runs before TimelineSource::advance()
 * each cycle, making recorded notes immediately available for playback.
 *
 * Thread model:
 *   start_recording / stop_recording — mutation thread only.
 *   is_recording                     — any thread (atomic).
 *   advance / on_locate              — timing thread only (called by engine).
 *
 * Note duration:
 *   Omega NOTE_ON events carry an inline duration (data[2..5]). Live recordings
 *   don't know a note's duration until its matching NOTE_OFF arrives. The
 *   Recorder holds a pending-note table keyed by (channel, pitch). When a
 *   NOTE_OFF arrives, the completed NOTE_ON (with correct duration) is inserted
 *   into the target track. Notes still pending when stop_recording() is called
 *   are flushed with duration = stop_tick - on_tick.
 *
 * Usage:
 *   omega::Recorder recorder{engine.timeline_source(), sink.sink_id()};
 *   engine.add_source(&recorder, OMEGA_SOURCE_PRIORITY_MODULATOR);
 *
 *   // From the mutation thread, after creating the target track:
 *   recorder.start_recording(track_id);
 *   // ... transport plays, notes flow in via the InputBus ...
 *   size_t n = recorder.stop_recording();
 */
class OMEGA_API Recorder final : public EventSource
{
public:
    /*
     * timeline — the engine's built-in TimelineSource; obtained via
     *            engine.timeline_source(). Must outlive this Recorder.
     * sink_id  — the OutputSink id that recorded events should route to.
     */
    explicit Recorder(TimelineSource& timeline, uint32_t sink_id) noexcept;

    ~Recorder() override = default;

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    /*
     * Arm recording to the given track. Any events on channel_filter (0-15)
     * are captured; pass 0xFF to capture all channels.
     * Thread: Mutation thread only.
     */
    void start_recording(TrackId target, uint8_t channel_filter = 0xFFu) noexcept;

    /*
     * Disarm recording. Flushes any notes that are still held (no NOTE_OFF yet)
     * using the tick of the last advance() call as the note-off tick. Returns
     * the number of NOTE_ON events inserted into the timeline.
     * Thread: Mutation thread only. Must not be called concurrently with advance().
     */
    size_t stop_recording() noexcept;

    /*
     * Returns true while recording is armed.
     * Thread: Any thread.
     */
    [[nodiscard]] bool is_recording() const noexcept
    {
        return recording_.load(std::memory_order_relaxed);
    }

    /* EventSource overrides — timing thread only. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    struct PendingNote
    {
        uint64_t on_tick{0};
        uint8_t velocity{0};
        bool active{false};
    };

    void flush_pending(uint64_t stop_tick) noexcept;

    TimelineSource& timeline_;
    uint32_t sink_id_;

    std::atomic<bool> recording_{false};
    TrackId target_{0};
    uint8_t channel_filter_{0xFFu};
    size_t events_captured_{0};
    uint64_t last_tick_{0};  // most recent to_tick seen by advance(); used by stop_recording

    // [channel 0-15][note 0-127]
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    PendingNote pending_[16][128]{};
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
};

}  // namespace omega
