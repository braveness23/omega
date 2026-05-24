#include <omega/engine.h>
#include <omega/marker_list.h>
#include <omega/omega.h>
#include <omega/tempo_map.h>
#include <omega/time_signature_map.h>
#include <omega/timeline.h>
#include <omega/track.h>
#include <omega/types.h>

#include <cstddef>
#include <cstring>

#include "MidiFile.h"

namespace omega
{

omega_status_t smf_export(Engine& engine, const char* path, int smf_type)
{
    if (path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    smf::MidiFile mf;
    mf.setTPQ(static_cast<int>(OMEGA_PPQN));

    // MidiFile starts with 1 track (track 0).
    // Type 1: track 0 = conductor (meta), tracks 1..N = omega tracks.
    // Type 0: everything in track 0.
    const auto& omega_tracks = engine.timeline_source().tracks();
    auto num_omega_tracks = static_cast<int>(omega_tracks.size());

    // Count non-empty omega tracks: the midifile library's write() calls back()
    // on every track's event list, which is UB (and crashes) on an empty list.
    // So we only allocate midifile tracks for omega tracks that actually have events.
    int non_empty_count = 0;
    for (int t = 0; t < num_omega_tracks; ++t)
    {
        if (!omega_tracks[static_cast<size_t>(t)].events.empty())
        {
            ++non_empty_count;
        }
    }

    if (smf_type == 1 && non_empty_count > 0)
    {
        mf.addTracks(non_empty_count);
    }

    // --- Export tempo map ---
    for (const auto& pt : engine.tempo_map().points())
    {
        if (pt.bpm_milli == 0u)
        {
            continue;
        }
        double bpm = static_cast<double>(pt.bpm_milli) / 1000.0;
        mf.addTempo(0, static_cast<int>(pt.tick), bpm);
    }

    // --- Export time signatures ---
    for (const auto& pt : engine.timesig_map().points())
    {
        mf.addTimeSignature(0, static_cast<int>(pt.tick), pt.numerator, pt.denominator);
    }

    // --- Export markers ---
    for (const auto& m : engine.marker_list().points())
    {
        mf.addMarker(0, static_cast<int>(m.tick), m.name);
    }

    // --- Export track events (skip empty tracks) ---
    int midi_track_seq = 1;
    for (int t = 0; t < num_omega_tracks; ++t)
    {
        const Track& tr = omega_tracks[static_cast<size_t>(t)];
        if (tr.events.empty())
        {
            continue;
        }

        int midi_track = (smf_type == 0) ? 0 : midi_track_seq++;

        for (const Event& ev : tr.events)
        {
            auto tick_i = static_cast<int>(ev.tick);

            if (ev.payload_tag == OMEGA_NOTE_ON)
            {
                uint32_t duration = 0u;
                std::memcpy(&duration, &ev.data[2], sizeof(duration));
                int off_tick = tick_i + static_cast<int>(duration);
                mf.addNoteOn(midi_track, tick_i, ev.channel, ev.data[0], ev.data[1]);
                mf.addNoteOff(midi_track, off_tick, ev.channel, ev.data[0], 0);
            }
            else if (ev.payload_tag == OMEGA_CC)
            {
                mf.addController(midi_track, tick_i, ev.channel, ev.data[0], ev.data[1]);
            }
            else if (ev.payload_tag == OMEGA_PROGRAM)
            {
                mf.addPatchChange(midi_track, tick_i, ev.channel, ev.data[0]);
            }
        }
    }

    mf.sortTracks();

    if (!mf.write(path))
    {
        return OMEGA_ERR_IO;
    }

    return OMEGA_OK;
}

}  // namespace omega
