#include <omega/clock_sync.h>
#include <omega/detail/spsc_queue.h>
#include <omega/event_input.h>
#include <omega/omega.h>
#include <omega/types.h>

#include <new>

#ifndef OMEGA_NO_HOST_MIDI
    #include <libremidi/libremidi.hpp>
    #include <string>
#endif

namespace omega
{

/* ── ClockMasterSource ────────────────────────────────────────────────────── */

namespace
{

Event make_rt_event(uint8_t tag, uint32_t sink_id) noexcept
{
    Event ev{};
    ev.payload_tag = tag;
    ev.sink_id = sink_id;
    return ev;
}

Event make_spp_event(uint32_t sink_id, uint64_t tick) noexcept
{
    constexpr uint32_t kTicksPer16th = PPQN / 4u;
    const auto spp = static_cast<uint32_t>(tick / kTicksPer16th);
    Event ev{};
    ev.payload_tag = OMEGA_MIDI_SPP;
    ev.sink_id = sink_id;
    ev.data[0] = static_cast<uint8_t>(spp & 0x7Fu);
    ev.data[1] = static_cast<uint8_t>((spp >> 7u) & 0x7Fu);
    return ev;
}

}  // namespace

ClockMasterSource::ClockMasterSource(Engine& engine, OutputSink& midi_out) noexcept
    : engine_{engine}, midi_out_{midi_out}
{
    engine_.set_event_callback(&ClockMasterSource::engine_cb, this);
}

ClockMasterSource::~ClockMasterSource()
{
    engine_.set_event_callback(nullptr, nullptr);
}

void ClockMasterSource::engine_cb(omega_engine_event_t event,
                                  uint32_t /*detail*/,
                                  void* ud) noexcept
{
    if (event != OMEGA_EVENT_TRANSPORT_STOPPED)
    {
        return;
    }
    auto* self = static_cast<ClockMasterSource*>(ud);
    Event fc = make_rt_event(OMEGA_MIDI_STOP_RT, self->midi_out_.sink_id());
    self->midi_out_.send(fc);
    self->first_advance_ = true;  // reset so next play re-emits FA/FB
}

void ClockMasterSource::advance(uint64_t to_tick,
                                EventDispatcher& /*dispatcher*/,
                                ProcessContext& /*ctx*/)
{
    const uint32_t sid = midi_out_.sink_id();

    if (first_advance_)
    {
        first_advance_ = false;
        // SPP → FA or FB
        midi_out_.send(make_spp_event(sid, pending_spp_));
        const uint8_t tag = pending_continue_ ? OMEGA_MIDI_CONTINUE : OMEGA_MIDI_START;
        midi_out_.send(make_rt_event(tag, sid));
        pending_continue_ = false;
        // First F8 at the locate tick.
        midi_out_.send(make_rt_event(OMEGA_MIDI_CLOCK, sid));
        prev_tick_ = pending_spp_;
    }

    // Emit F8 for every TICKS_PER_CLOCK boundary in (prev_tick_, to_tick].
    const uint64_t first_clock = (prev_tick_ / TICKS_PER_CLOCK + 1u) * TICKS_PER_CLOCK;
    for (uint64_t t = first_clock; t <= to_tick; t += TICKS_PER_CLOCK)
    {
        midi_out_.send(make_rt_event(OMEGA_MIDI_CLOCK, sid));
    }
    prev_tick_ = to_tick;
}

void ClockMasterSource::on_locate(uint64_t tick,
                                  EventDispatcher& /*dispatcher*/,
                                  ProcessContext& /*ctx*/)
{
    first_advance_ = true;
    pending_spp_ = tick;
    pending_continue_ = (tick != 0u);
}

/* ── ClockSlaveSource ─────────────────────────────────────────────────────── */

ClockSlaveSource::ClockSlaveSource(Engine& engine) noexcept : engine_{engine} {}

void ClockSlaveSource::advance(uint64_t to_tick,
                               EventDispatcher& /*dispatcher*/,
                               ProcessContext& ctx)
{
    if (ctx.input_bus == nullptr)
    {
        return;
    }

    const uint32_t n = ctx.input_bus->count();
    for (uint32_t i = 0u; i < n; ++i)
    {
        const Event& ev = ctx.input_bus->at(i);
        switch (ev.payload_tag)
        {
            case OMEGA_MIDI_CLOCK:
            {
                if (!has_last_f8_)
                {
                    has_last_f8_ = true;
                    last_f8_tick_ = to_tick;
                    break;
                }
                const auto raw = static_cast<uint32_t>(to_tick - last_f8_tick_);
                last_f8_tick_ = to_tick;
                if (raw == 0u)
                {
                    break;
                }
                interval_buf_[interval_idx_] = raw;
                interval_idx_ = (interval_idx_ + 1u) % SMOOTH_WINDOW;
                if (interval_count_ < SMOOTH_WINDOW)
                {
                    ++interval_count_;
                }
                // Average interval over the window.
                uint64_t sum = 0u;
                for (uint32_t j = 0u; j < interval_count_; ++j)
                {
                    sum += interval_buf_[j];
                }
                const auto avg = static_cast<uint32_t>(sum / interval_count_);
                if (avg == 0u)
                {
                    break;
                }
                // Derive BPM: ideal interval = PPQN/24; scale from current BPM.
                const uint32_t cur_bpm = engine_.tempo_map().bpm_milli_at(to_tick);
                const auto new_bpm =
                    static_cast<uint32_t>(static_cast<uint64_t>(cur_bpm) * (PPQN / 24u) / avg);
                if (new_bpm > 0u && new_bpm < 500'000u)
                {
                    engine_.sync_external_tempo(new_bpm);
                }
                break;
            }
            case OMEGA_MIDI_START:
                interval_count_ = 0u;
                has_last_f8_ = false;
                engine_.sync_external_locate(0u);
                engine_.sync_external_play();
                break;
            case OMEGA_MIDI_CONTINUE:
                engine_.sync_external_play();
                break;
            case OMEGA_MIDI_STOP_RT:
                engine_.sync_external_stop();
                break;
            case OMEGA_MIDI_SPP:
            {
                const uint32_t spp =
                    (static_cast<uint32_t>(ev.data[1]) << 7u) | static_cast<uint32_t>(ev.data[0]);
                engine_.sync_external_locate(static_cast<uint64_t>(spp) * (PPQN / 4u));
                break;
            }
            default:
                break;
        }
    }
}

/* ── ClockSlaveInput ──────────────────────────────────────────────────────── */

#ifndef OMEGA_NO_HOST_MIDI

namespace
{

bool clock_midi_to_event(const libremidi::message& msg, Event& out) noexcept
{
    if (msg.bytes.empty())
    {
        return false;
    }
    out = Event{};
    switch (msg.bytes[0])
    {
        case 0xF8u:
            out.payload_tag = OMEGA_MIDI_CLOCK;
            return true;
        case 0xFAu:
            out.payload_tag = OMEGA_MIDI_START;
            return true;
        case 0xFBu:
            out.payload_tag = OMEGA_MIDI_CONTINUE;
            return true;
        case 0xFCu:
            out.payload_tag = OMEGA_MIDI_STOP_RT;
            return true;
        case 0xF2u:
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            out.payload_tag = OMEGA_MIDI_SPP;
            out.data[0] = msg.bytes[1] & 0x7Fu;
            out.data[1] = msg.bytes[2] & 0x7Fu;
            return true;
        default:
            return false;
    }
}

}  // namespace

struct ClockSlaveInput::Impl
{
    detail::SpscQueue<Event, 256> queue;
    libremidi::midi_in midi_in;
    bool port_open{false};

    explicit Impl(const char* port_name) noexcept
        : midi_in{[this]() {
              libremidi::input_configuration cfg;
              cfg.on_message = [this](const libremidi::message& msg) noexcept {
                  Event e{};
                  if (clock_midi_to_event(msg, e))
                  {
                      queue.push(e);
                  }
              };
              cfg.ignore_sysex = true;
              cfg.ignore_timing = false;  // receive F8 clock bytes
              cfg.ignore_sensing = true;
              return cfg;
          }()}
    {
        try
        {
            if (port_name == nullptr)
            {
                libremidi::observer obs{};
                auto ports = obs.get_input_ports();
                if (!ports.empty())
                {
                    if (auto err = midi_in.open_port(ports[0], "Omega Clock In"); !err.is_set())
                    {
                        port_open = true;
                    }
                }
            }
            else if (*port_name == '\0')
            {
                if (auto err = midi_in.open_virtual_port("Omega Clock In"); !err.is_set())
                {
                    port_open = true;
                }
            }
            else
            {
                const std::string name{port_name};
                libremidi::observer obs{};
                for (auto& p : obs.get_input_ports())
                {
                    if (p.port_name == name || p.display_name == name)
                    {
                        if (auto err = midi_in.open_port(p, "Omega Clock In"); !err.is_set())
                        {
                            port_open = true;
                        }
                        break;
                    }
                }
            }
        }
        catch (...)
        {}
    }
};

ClockSlaveInput::ClockSlaveInput(const char* port_name) noexcept
    : impl_{new(std::nothrow) Impl{port_name}}  // NOLINT(cppcoreguidelines-owning-memory)
{}

ClockSlaveInput::~ClockSlaveInput()
{
    delete impl_;  // NOLINT(cppcoreguidelines-owning-memory)
}

void ClockSlaveInput::poll(InputDispatcher& dispatcher)
{
    if (impl_ == nullptr)
    {
        return;
    }
    Event ev{};
    while (impl_->queue.pop(ev))
    {
        dispatcher.deliver(ev);
    }
}

#else /* OMEGA_NO_HOST_MIDI */

struct ClockSlaveInput::Impl
{};

ClockSlaveInput::ClockSlaveInput(const char* /*port_name*/) noexcept {}
ClockSlaveInput::~ClockSlaveInput() {}
void ClockSlaveInput::poll(InputDispatcher& /*dispatcher*/) {}

#endif /* OMEGA_NO_HOST_MIDI */

}  // namespace omega
