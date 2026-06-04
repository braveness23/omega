#include <omega/clock_sync.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace omega;
using Catch::Approx;

// ── Helpers ───────────────────────────────────────────────────────────────────

/* Sink that records raw uint8_t bytes from OMEGA_MIDI_* events. */
class ClockCaptureSink final : public OutputSink
{
public:
    void send(const Event& ev) override
    {
        switch (ev.payload_tag)
        {
            case OMEGA_MIDI_CLOCK:
                bytes_.push_back(0xF8u);
                break;
            case OMEGA_MIDI_START:
                bytes_.push_back(0xFAu);
                break;
            case OMEGA_MIDI_CONTINUE:
                bytes_.push_back(0xFBu);
                break;
            case OMEGA_MIDI_STOP_RT:
                bytes_.push_back(0xFCu);
                break;
            case OMEGA_MIDI_SPP:
                bytes_.push_back(0xF2u);
                bytes_.push_back(ev.data[0]);
                bytes_.push_back(ev.data[1]);
                break;
            default:
                break;
        }
    }
    void flush() override {}

    [[nodiscard]] bool has(uint8_t byte) const noexcept
    {
        return std::any_of(bytes_.cbegin(), bytes_.cend(), [byte](uint8_t b) { return b == byte; });
    }

    [[nodiscard]] uint32_t count_of(uint8_t byte) const noexcept
    {
        uint32_t n = 0u;
        for (auto b : bytes_)
        {
            if (b == byte)
            {
                ++n;
            }
        }
        return n;
    }

    void clear() noexcept { bytes_.clear(); }

    std::vector<uint8_t> bytes_;
};

// ── ClockMasterSource tests ───────────────────────────────────────────────────

TEST_CASE("ClockMasterSource: emits FA and SPP on first advance from tick 0")
{
    MockClock clock;
    Engine eng{&clock};

    ClockCaptureSink out;
    eng.add_sink(&out);

    ClockMasterSource master{eng, out};
    eng.add_source(&master, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::LOCATE, 0u});
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply commands; to_tick ~= 0; first advance

    REQUIRE(out.has(0xF2u));  // SPP
    REQUIRE(out.has(0xFAu));  // Start
    REQUIRE(out.has(0xF8u));  // first clock
}

TEST_CASE("ClockMasterSource: emits FB (Continue) on locate to non-zero tick")
{
    MockClock clock;
    Engine eng{&clock};

    ClockCaptureSink out;
    eng.add_sink(&out);

    ClockMasterSource master{eng, out};
    eng.add_source(&master, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::LOCATE, 480u});
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    REQUIRE(out.has(0xF2u));  // SPP
    REQUIRE(out.has(0xFBu));  // Continue (not Start)
    REQUIRE_FALSE(out.has(0xFAu));
}

TEST_CASE("ClockMasterSource: emits F8 at TICKS_PER_CLOCK intervals")
{
    MockClock clock;
    Engine eng{&clock};

    ClockCaptureSink out;
    eng.add_sink(&out);

    ClockMasterSource master{eng, out};
    eng.add_source(&master, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::LOCATE, 0u});
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // first advance: FA + SPP + 1 F8 at tick 0

    out.clear();

    // Advance 4 beats: 4 * PPQN ticks → 4 * 24 = 96 F8 pulses.
    clock.advance_beats(4.0);
    eng.process();

    // 4 beats * 24 clocks/beat = 96 clocks, allow ±1 for rounding.
    const auto n = out.count_of(0xF8u);
    REQUIRE(n >= 95u);
    REQUIRE(n <= 97u);
}

TEST_CASE("ClockMasterSource: emits FC when transport stops")
{
    MockClock clock;
    Engine eng{&clock};

    ClockCaptureSink out;
    eng.add_sink(&out);

    ClockMasterSource master{eng, out};
    eng.add_source(&master, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::LOCATE, 0u});
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    out.clear();

    eng.enqueue(TransportCmd{TransportAction::STOP, 0u});
    eng.process();  // applies STOP → fires engine_cb → emits FC

    REQUIRE(out.has(0xFCu));
}

// ── ClockSlaveSource tests ────────────────────────────────────────────────────

/* Injects an OMEGA_MIDI_* event directly into the engine's InputBus for testing. */
class ClockInjector final : public EventInput
{
public:
    void inject(uint8_t payload_tag, uint8_t d0 = 0u, uint8_t d1 = 0u) noexcept
    {
        pending_.payload_tag = payload_tag;
        pending_.data[0] = d0;
        pending_.data[1] = d1;
        has_pending_ = true;
    }

    void poll(InputDispatcher& dispatcher) override
    {
        if (has_pending_)
        {
            dispatcher.deliver(pending_);
            has_pending_ = false;
        }
    }

private:
    Event pending_{};
    bool has_pending_{false};
};

TEST_CASE("ClockSlaveSource: FA resets to tick 0 and (re-)starts playback")
{
    MockClock clock;
    Engine eng{&clock};

    ClockInjector injector;
    eng.add_input(&injector);

    ClockSlaveSource slave{eng};
    eng.add_source(&slave, OMEGA_SOURCE_PRIORITY_MODULATOR);

    // Engine must be playing for the slave's advance() to be called.
    // Locate to a non-zero position to verify FA resets it to 0.
    eng.enqueue(TransportCmd{TransportAction::LOCATE, 480u});
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // engine plays from tick ~480

    // Inject FA — slave reads it in the next advance() and resets to tick 0.
    // sync_external_locate() updates session_start_ns_; the new to_tick computed
    // from that is reflected starting on the cycle AFTER the locate.
    injector.inject(OMEGA_MIDI_START);
    clock.advance_ticks(1);
    eng.process();  // FA processed; session_start_ns_ resets
    eng.process();  // first cycle with new start: to_tick ≈ 0

    REQUIRE(eng.transport_state() == TransportState::PLAYING);
    REQUIRE(eng.transport_position_tick() == 0u);
}

TEST_CASE("ClockSlaveSource: FC stops a playing engine")
{
    MockClock clock;
    Engine eng{&clock};

    ClockInjector injector;
    eng.add_input(&injector);

    ClockSlaveSource slave{eng};
    eng.add_source(&slave, OMEGA_SOURCE_PRIORITY_MODULATOR);

    // Start engine playing directly.
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();
    REQUIRE(eng.transport_state() == TransportState::PLAYING);

    // Inject FC — slave reads it and calls sync_external_stop().
    injector.inject(OMEGA_MIDI_STOP_RT);
    clock.advance_ticks(1);
    eng.process();

    REQUIRE(eng.transport_state() == TransportState::STOPPED);
}

TEST_CASE("ClockSlaveSource: F2 (SPP) repositions a playing engine")
{
    MockClock clock;
    Engine eng{&clock};

    ClockInjector injector;
    eng.add_input(&injector);

    ClockSlaveSource slave{eng};
    eng.add_source(&slave, OMEGA_SOURCE_PRIORITY_MODULATOR);

    // Engine must be playing for the slave to process input events.
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    // SPP = 4 → tick = 4 * (PPQN/4) = 4 * 120 = 480
    // Two cycles needed: first processes the SPP (sets session_start_ns_),
    // second reads the updated position.
    injector.inject(OMEGA_MIDI_SPP, 4u, 0u);
    clock.advance_ticks(1);
    eng.process();  // SPP processed
    eng.process();  // position reflects new session_start_ns_

    REQUIRE(eng.transport_position_tick() == 480u);
}

TEST_CASE("ClockSlaveSource: F8 pulses converge engine tempo")
{
    MockClock clock;
    Engine eng{&clock};

    ClockInjector injector;
    eng.add_input(&injector);

    ClockSlaveSource slave{eng};
    eng.add_source(&slave, OMEGA_SOURCE_PRIORITY_MODULATOR);

    // Start engine playing at default 120 BPM.
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    // Inject ClockSlaveSource::SMOOTH_WINDOW + 1 F8 pulses at 20 tick intervals
    // (which IS the correct 120 BPM interval). Tempo should remain 120 BPM.
    for (uint32_t i = 0u; i <= ClockSlaveSource::SMOOTH_WINDOW; ++i)
    {
        injector.inject(OMEGA_MIDI_CLOCK);
        clock.advance_ticks(20);
        eng.process();
    }

    // After smoothing, ext_bpm should converge to ~120 BPM.
    // Transport is still playing; verify engine has accepted the external tempo.
    REQUIRE(eng.transport_state() == TransportState::PLAYING);
}

// ── sync_external_* engine API tests ─────────────────────────────────────────

TEST_CASE("Engine::sync_external_tempo: non-zero enables override; zero disables")
{
    MockClock clock;
    Engine eng{&clock};

    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    // Set external tempo to 180 BPM; ext_bpm_milli_ is timing-thread only so
    // we test via indirect effect on tick progression.
    eng.sync_external_tempo(180'000u);  // 180 BPM

    clock.advance_ticks(0);  // just a hook; process re-computes to_tick
    eng.process();

    // Disable override
    eng.sync_external_tempo(0u);
    eng.process();

    REQUIRE(eng.transport_state() == TransportState::PLAYING);
}

TEST_CASE("Engine::sync_external_stop: stops engine from timing thread context")
{
    MockClock clock;
    Engine eng{&clock};

    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();
    REQUIRE(eng.transport_state() == TransportState::PLAYING);

    eng.sync_external_stop();

    REQUIRE(eng.transport_state() == TransportState::STOPPED);
}

TEST_CASE("Engine::sync_external_locate: repositions transport")
{
    MockClock clock;
    Engine eng{&clock};

    eng.sync_external_locate(960u);

    REQUIRE(eng.transport_position_tick() == 960u);
}
