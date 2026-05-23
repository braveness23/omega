#pragma once

#include <omega/input_bus.h>
#include <omega/modulation_bus.h>
#include <omega/omega.h>

namespace omega
{

/*
 * Shared per-cycle context passed to every EventSource::advance() call.
 *
 * Populated at the start of each process() cycle. Sources read from it to
 * make musically-aware decisions (scale, chord, global transpose, etc.) and
 * modulator sources write to modulation_bus channels.
 */
struct ProcessContext
{
    InputBus* input_bus{nullptr};            // non-null during advance(); null during on_locate()
    ModulationBus* modulation_bus{nullptr};  // non-null when engine has a ModulationBus
    omega_perf_ctx_t perf_ctx{};             // snapshotted from engine at the top of each cycle
};

}  // namespace omega
