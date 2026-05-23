#pragma once

namespace omega
{

/*
 * Shared per-cycle context passed to every EventSource::advance() call.
 *
 * Populated at the start of each process() cycle. Sources read from it to
 * make musically-aware decisions (scale, chord, global transpose, etc.) and
 * modulator sources write to ModulationBus channels.
 *
 * This struct is a stub for M2. Full fields (ModulationBus, PerformanceContext,
 * InputBus) are added in M4.
 */
struct ProcessContext
{
    /* Populated in M4. */
};

}  // namespace omega
