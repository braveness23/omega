#pragma once

#include <omega/omega.h>

#include <cstddef>
#include <cstdint>
#include <vector>

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
 * Buffer overload (W5 fix): serialize to a byte vector instead of a file path.
 * `out` is cleared and replaced with the session bytes on success.
 * Avoids the need for a filesystem path (useful in WASM / plugin contexts).
 *
 * Thread: Mutation thread only, engine stopped.
 *
 * Returns:
 *   OMEGA_OK     — saved successfully.
 *   OMEGA_ERR_IO — serialization failure.
 */
omega_status_t session_save(Engine& engine, std::vector<uint8_t>& out);

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

/*
 * Buffer overload (W5 fix): restore from raw bytes already in memory.
 * Avoids the need for a filesystem path (useful in WASM / plugin contexts).
 *
 * Thread: Mutation thread only, engine stopped.
 *
 * Returns:
 *   OMEGA_OK          — loaded successfully.
 *   OMEGA_ERR_INVALID — data is null or size is 0.
 *   OMEGA_ERR_IO      — data is corrupt or format is unrecognised.
 */
omega_status_t session_load(Engine& engine, const uint8_t* data, size_t size);

}  // namespace omega
