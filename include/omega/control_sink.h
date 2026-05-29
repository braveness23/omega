#pragma once

#include <omega/export.h>
#include <omega/sink.h>
#include <omega/types.h>

namespace omega
{

class PerformanceSource;
class TempoMap;

/*
 * An OutputSink that executes control-sequence events (OMEGA_CTRL_*) as direct
 * engine mutations rather than sending MIDI bytes.
 *
 * Register one ControlSink with the engine alongside the MIDI output sink.
 * Control patterns use ctrl_sink.sink_id() in their events' sink_id field;
 * the PerformanceSource routes those events here during advance().
 *
 * On receive:
 *   OMEGA_CTRL_START_SLOT  — calls perf_.cue(slot, mode, event.tick)
 *   OMEGA_CTRL_STOP_SLOT   — calls perf_.stop(slot, mode, event.tick)
 *   OMEGA_CTRL_SET_TEMPO   — calls tempo_.insert(event.tick, bpm_milli)
 *   OMEGA_CTRL_TRANSPOSE   — calls perf_.set_transpose(slot, semitones)
 *   All other tags          — silently dropped
 *
 * Thread safety: send() and flush() are called from the timing thread and must
 * never allocate, block, or lock. The methods called on perf_ and tempo_ are
 * safe on the timing thread (they are called directly by Engine::apply()
 * overloads during the same process() cycle).
 */
class OMEGA_API ControlSink final : public OutputSink
{
public:
    /*
     * perf and tempo must outlive this ControlSink. Obtain them via
     * engine.perf_source() and engine.tempo_map().
     */
    ControlSink(PerformanceSource& perf, TempoMap& tempo) noexcept;

    ~ControlSink() override = default;

    ControlSink(const ControlSink&) = delete;
    ControlSink& operator=(const ControlSink&) = delete;
    ControlSink(ControlSink&&) = delete;
    ControlSink& operator=(ControlSink&&) = delete;

    void send(const Event& event) override;
    void flush() override {}

private:
    PerformanceSource& perf_;
    TempoMap& tempo_;
};

}  // namespace omega
