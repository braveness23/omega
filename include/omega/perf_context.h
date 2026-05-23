#pragma once

#include <omega/omega.h>

namespace omega
{

/*
 * Performance context — global musical state snapshotted each process() cycle.
 * omega_perf_ctx_t is the canonical type (defined in omega.h); this alias lets
 * C++ sources use the shorter name.
 */
using PerfContext = omega_perf_ctx_t;

}  // namespace omega
