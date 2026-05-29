#include <omega/control_sink.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/tempo_map.h>
#include <omega/types.h>

#include <cstring>

namespace omega
{

ControlSink::ControlSink(PerformanceSource& perf, TempoMap& tempo) noexcept
    : perf_{perf}, tempo_{tempo}
{}

void ControlSink::send(const Event& event)
{
    switch (event.payload_tag)
    {
        case OMEGA_CTRL_START_SLOT:
        {
            uint32_t slot = 0;
            std::memcpy(&slot, &event.data[0], sizeof(slot));
            perf_.cue(slot, static_cast<CueMode>(event.data[4]), event.tick);
            break;
        }
        case OMEGA_CTRL_STOP_SLOT:
        {
            uint32_t slot = 0;
            std::memcpy(&slot, &event.data[0], sizeof(slot));
            perf_.stop(slot, static_cast<CueMode>(event.data[4]), event.tick);
            break;
        }
        case OMEGA_CTRL_SET_TEMPO:
        {
            uint32_t bpm = 0;
            std::memcpy(&bpm, &event.data[0], sizeof(bpm));
            if (bpm > 0)
            {
                tempo_.insert(event.tick, bpm);
            }
            break;
        }
        case OMEGA_CTRL_TRANSPOSE:
        {
            uint32_t slot = 0;
            std::memcpy(&slot, &event.data[0], sizeof(slot));
            perf_.set_transpose(slot, static_cast<int8_t>(event.data[4]));
            break;
        }
        default:
            // Non-control events silently dropped — ControlSink does not send MIDI.
            break;
    }
}

}  // namespace omega
