#pragma once

#include <omega/omega.h>

namespace omega
{

class Engine;

/*
 * Serialize the full engine session to a file.
 *
 * Captures all engine state not covered by SMF: PatternLibrary,
 * PerformanceSource slot assignments, RegionList, PerformanceContext,
 * transport loop region, and everything SMF also covers (tempo map,
 * time signatures, timeline tracks, markers). The format is versioned
 * so newer omega can load sessions written by older versions.
 *
 * Writes atomically: data goes to a temp file and is renamed into place.
 *
 * Thread: Mutation thread only, engine stopped.
 *
 * Returns:
 *   OMEGA_OK          — saved successfully.
 *   OMEGA_ERR_INVALID — path is NULL.
 *   OMEGA_ERR_IO      — file write failure.
 */
omega_status_t session_save(Engine& engine, const char* path);

/*
 * Restore a previously saved session. Replaces ALL existing engine state:
 * timeline tracks, patterns, perf slots, song arrangement, tempo map,
 * time signatures, markers, regions, loop region, SMPTE config, and
 * performance context.
 *
 * Thread: Mutation thread only, engine stopped.
 *
 * Returns:
 *   OMEGA_OK          — loaded successfully.
 *   OMEGA_ERR_INVALID — path is NULL.
 *   OMEGA_ERR_IO      — file not found, unreadable, or corrupt.
 */
omega_status_t session_load(Engine& engine, const char* path);

}  // namespace omega
