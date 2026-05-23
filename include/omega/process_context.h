#pragma once

#include <omega/input_bus.h>

namespace omega
{

/*
 * Shared per-cycle context passed to every EventSource::advance() call.
 *
 * Populated at the start of each process() cycle. Sources read from it to
 * make musically-aware decisions (scale, chord, global transpose, etc.) and
 * modulator sources write to ModulationBus channels.
 */
struct ProcessContext
{
    InputBus* input_bus{nullptr};  // non-null during advance(); null during on_locate()
};

}  // namespace omega
