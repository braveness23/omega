#include <omega/control_sink.h>
#include <omega/engine.h>

namespace omega
{

ControlSink::ControlSink(Engine& engine) noexcept : engine_{engine} {}

void ControlSink::send(const Event& event)
{
    engine_.execute_ctrl_event(event);
}

}  // namespace omega
