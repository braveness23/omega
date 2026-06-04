#pragma once

#include <omega/engine.h>
#include <omega/event_input.h>
#include <omega/event_source.h>
#include <omega/export.h>
#include <omega/sink.h>

#include <cstdint>

namespace omega
{

/* ── ClockMasterSource ────────────────────────────────────────────────────── */

/*
 * Generates MIDI clock output events (F8/FA/FB/FC/SPP) sent directly to a
 * downstream OutputSink that understands OMEGA_MIDI_* payload tags (e.g. a
 * LibremidiSink registered with the same engine).
 *
 * Registers itself as the engine event callback to detect transport stop and
 * emit FC. The previous callback (if any) is replaced; the constructor saves
 * the caller's responsibility to avoid double-registration.
 *
 * Thread model:
 *   advance() / on_locate() — timing thread only.
 *   engine_cb()             — timing thread only (called from process()).
 *   Constructor / destructor — mutation thread only.
 */
class OMEGA_API ClockMasterSource final : public EventSource
{
public:
    /* MIDI clock pulse every PPQN / 24 = 20 ticks. */
    static constexpr uint32_t TICKS_PER_CLOCK = 20u;

    /*
     * engine   — must outlive this source.
     * midi_out — OutputSink receiving MIDI clock events (e.g. LibremidiSink).
     *            Must remain valid while this source is registered.
     */
    explicit ClockMasterSource(Engine& engine, OutputSink& midi_out) noexcept;
    ~ClockMasterSource() override;

    ClockMasterSource(const ClockMasterSource&) = delete;
    ClockMasterSource& operator=(const ClockMasterSource&) = delete;
    ClockMasterSource(ClockMasterSource&&) = delete;
    ClockMasterSource& operator=(ClockMasterSource&&) = delete;

    /* Timing thread. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    static void engine_cb(omega_engine_event_t event, uint32_t detail, void* ud) noexcept;

    Engine& engine_;
    OutputSink& midi_out_;

    /* Transport tracking — timing thread only. */
    bool first_advance_{true};     /* true = emit FA/FB + SPP on next advance */
    bool pending_continue_{false}; /* true = emit FB instead of FA on first advance */
    uint64_t pending_spp_{0u};     /* tick position for SPP on first advance */
    uint64_t prev_tick_{0u};       /* last processed tick for F8 interval tracking */
};

/* ── ClockSlaveSource ─────────────────────────────────────────────────────── */

/*
 * Reads MIDI clock bytes (F8/FA/FB/FC/F2) from the engine's InputBus and
 * adjusts the engine tempo and transport to follow the external clock.
 *
 * Use ClockSlaveInput (below) as the EventInput that delivers MIDI clock bytes
 * to the InputBus before this source's advance() reads them.
 *
 * Tempo tracking: smooths F8 pulse intervals over SMOOTH_WINDOW pulses and
 * calls engine.sync_external_tempo() each window. Pass 0 to sync_external_tempo
 * to return to TempoMap-driven timing when done slaving.
 *
 * Thread model:
 *   advance() — timing thread only.
 *   Constructor / destructor — mutation thread only.
 */
class OMEGA_API ClockSlaveSource final : public EventSource
{
public:
    static constexpr uint32_t SMOOTH_WINDOW = 4u; /* F8 pulses to average */

    explicit ClockSlaveSource(Engine& engine) noexcept;
    ~ClockSlaveSource() override = default;

    ClockSlaveSource(const ClockSlaveSource&) = delete;
    ClockSlaveSource& operator=(const ClockSlaveSource&) = delete;
    ClockSlaveSource(ClockSlaveSource&&) = delete;
    ClockSlaveSource& operator=(ClockSlaveSource&&) = delete;

    /* Timing thread. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    Engine& engine_;

    /* F8 pulse tracking — timing thread only. */
    uint64_t last_f8_tick_{0u};
    bool has_last_f8_{false};
    uint32_t interval_buf_[SMOOTH_WINDOW]{};
    uint32_t interval_idx_{0u};
    uint32_t interval_count_{0u};
};

/* ── ClockSlaveInput ──────────────────────────────────────────────────────── */

/*
 * EventInput that opens a MIDI port with timing messages enabled and delivers
 * F8/FA/FB/FC/F2 bytes as omega events (OMEGA_MIDI_* payload tags) to the
 * InputBus. Pair with a registered ClockSlaveSource to drive MIDI sync slaving.
 *
 * Unavailable when built with OMEGA_NO_HOST_MIDI — poll() is a no-op.
 *
 * Thread model:
 *   poll() — timing thread only.
 *   Constructor / destructor — mutation thread only.
 */
class OMEGA_API ClockSlaveInput final : public EventInput
{
public:
    /*
     * port_name — MIDI input port (same semantics as LibremidiInput).
     *   NULL   — first available port.
     *   ""     — open a virtual port named "Omega Clock In".
     *   other  — match by display or port name.
     */
    explicit ClockSlaveInput(const char* port_name) noexcept;
    ~ClockSlaveInput() override;

    ClockSlaveInput(const ClockSlaveInput&) = delete;
    ClockSlaveInput& operator=(const ClockSlaveInput&) = delete;
    ClockSlaveInput(ClockSlaveInput&&) = delete;
    ClockSlaveInput& operator=(ClockSlaveInput&&) = delete;

    /* Timing thread. */
    void poll(InputDispatcher& dispatcher) override;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

}  // namespace omega
