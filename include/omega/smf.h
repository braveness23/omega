#pragma once

#include <omega/omega.h>

#include <cstdint>

namespace omega
{

class Engine;

/*
 * Options controlling smf_import.
 *
 * sink_id          — route every imported event (and the tracks that hold them)
 *                    to this OutputSink id. Imported events default to sink_id 0,
 *                    which matches no registered sink (ids start at 1), so set
 *                    this to the id of the sink you want playback to reach.
 *                    Leave 0 to preserve the legacy "unrouted" behaviour.
 * split_by_channel — when true, split each SMF track into one omega track per
 *                    MIDI channel it uses. A Type 0 file (all channels in one
 *                    track) then imports as up to 16 channel tracks, which a
 *                    multi-track UI can mute/solo independently. When false, one
 *                    omega track is created per SMF track (the default).
 */
struct SmfImportOptions
{
    uint32_t sink_id = 0;
    bool split_by_channel = false;
    /*
     * When true, clears the engine's timeline tracks, tempo map (reset to
     * 120 BPM), time-signature map, and marker list before importing. Use for
     * a "replace session" load so the new file replaces existing content.
     * Default false (appends to existing content).
     */
    bool clear_existing = false;
};

/*
 * Imports a Standard MIDI File (Type 0 or Type 1) into the engine's
 * TimelineSource, also populating the tempo map, time-signature map, and
 * markers. Each created Track has its channel and sink_id set per `opts`.
 * The engine must be stopped.
 *
 * Returns OMEGA_OK, OMEGA_ERR_INVALID (null path), or OMEGA_ERR_IO (read/parse
 * failure).
 *
 * Thread: Mutation thread only, engine stopped.
 */
omega_status_t smf_import(Engine& engine, const char* path, const SmfImportOptions& opts);

/* Convenience overload: import with default options (sink_id 0, no split). */
omega_status_t smf_import(Engine& engine, const char* path);

/*
 * Exports the engine session to a Standard MIDI File. smf_type is 0 (single
 * merged track) or 1 (multi-track; track 0 carries tempo/meter/markers). The
 * engine must be stopped.
 *
 * Returns OMEGA_OK, OMEGA_ERR_INVALID (null path), or OMEGA_ERR_IO (write
 * failure).
 *
 * Thread: Mutation thread only, engine stopped.
 */
omega_status_t smf_export(Engine& engine, const char* path, int smf_type);

}  // namespace omega
