#pragma once

#include <omega/export.h>
#include <omega/sink.h>
#include <omega/types.h>

namespace omega
{

class Engine;

/*
 * An OutputSink that executes control-sequence events (OMEGA_CTRL_*) as engine
 * mutations rather than sending MIDI bytes.
 *
 * Register one ControlSink with the engine alongside the MIDI output sink.
 * Control patterns use ctrl_sink.sink_id() in their events' sink_id field;
 * the PerformanceSource routes those events here during advance().
 *
 * On receive, delegates to Engine::execute_ctrl_event():
 *   OMEGA_CTRL_START_SLOT  — cues the target slot
 *   OMEGA_CTRL_STOP_SLOT   — stops the target slot
 *   OMEGA_CTRL_SET_TEMPO   — inserts a tempo point
 *   OMEGA_CTRL_TRANSPOSE   — sets per-slot transpose
 *   All other tags          — silently dropped
 *
 * Thread safety: send() and flush() are called from the timing thread and must
 * never allocate, block, or lock. Engine::execute_ctrl_event() has the same
 * timing-thread-only contract.
 */
class OMEGA_API ControlSink final : public OutputSink
{
public:
    /*
     * engine must outlive this ControlSink.
     */
    explicit ControlSink(Engine& engine) noexcept;

    ~ControlSink() override = default;

    ControlSink(const ControlSink&) = delete;
    ControlSink& operator=(const ControlSink&) = delete;
    ControlSink(ControlSink&&) = delete;
    ControlSink& operator=(ControlSink&&) = delete;

    void send(const Event& event) override;
    void flush() override {}

private:
    Engine& engine_;
};

}  // namespace omega
