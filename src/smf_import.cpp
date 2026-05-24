#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/types.h>

#include <cstring>
#include <string>

#include "MidiFile.h"

namespace omega
{

omega_status_t smf_import(Engine& engine, const char* path)
{
    if (path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    smf::MidiFile mf;
    if (!mf.read(path))
    {
        return OMEGA_ERR_IO;
    }

    mf.makeAbsoluteTicks();
    mf.linkNotePairs();

    int smf_ppqn = mf.getTPQ();
    if (smf_ppqn <= 0)
    {
        return OMEGA_ERR_IO;
    }

    int num_tracks = mf.getNumTracks();

    for (int t = 0; t < num_tracks; ++t)
    {
        TrackId track_id = engine.add_track("track_" + std::to_string(t));

        for (int e = 0; e < mf[t].size(); ++e)
        {
            const smf::MidiEvent& ev = mf[t][e];
            uint64_t omega_tick =
                (static_cast<uint64_t>(ev.tick) * static_cast<uint64_t>(OMEGA_PPQN)) /
                static_cast<uint64_t>(smf_ppqn);

            if (ev.isTempo())
            {
                int usec = ev.getTempoMicroseconds();
                if (usec > 0)
                {
                    uint32_t bpm_milli =
                        static_cast<uint32_t>(60'000'000'000ULL / static_cast<uint64_t>(usec));
                    engine.tempo_map().insert(omega_tick, bpm_milli);
                }
            }
            else if (ev.isTimeSignature())
            {
                // FF 58 04 <num> <denom-log2> <clocks> <32nds>
                if (ev.size() == 7)
                {
                    uint8_t num = static_cast<uint8_t>(ev[3]);
                    uint8_t denom = static_cast<uint8_t>(1 << ev[4]);
                    engine.timesig_map().insert(omega_tick, num, denom);
                }
            }
            else if (ev.isMeta())
            {
                int meta_type = ev.getMetaType();
                if (meta_type == 0x06 || meta_type == 0x07)
                {
                    std::string name = ev.getMetaContent();
                    if (meta_type == 0x07)
                    {
                        name = "[cue] " + name;
                    }
                    engine.marker_list().add(std::move(name), omega_tick);
                }
            }
            else if (ev.isNoteOn())
            {
                uint8_t channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                uint8_t note = static_cast<uint8_t>(ev.getP1());
                uint8_t velocity = static_cast<uint8_t>(ev.getP2());

                uint32_t duration = 0u;
                if (ev.isLinked())
                {
                    int smf_dur = ev.getTickDuration();
                    if (smf_dur > 0)
                    {
                        duration = static_cast<uint32_t>(
                            (static_cast<uint64_t>(smf_dur) * static_cast<uint64_t>(OMEGA_PPQN)) /
                            static_cast<uint64_t>(smf_ppqn));
                    }
                }

                Event note_ev{};
                note_ev.tick = omega_tick;
                note_ev.sink_id = 0u;
                note_ev.payload_tag = OMEGA_NOTE_ON;
                note_ev.channel = channel;
                note_ev.data[0] = note;
                note_ev.data[1] = velocity;
                std::memcpy(&note_ev.data[2], &duration, sizeof(duration));

                engine.add_track_event(track_id, note_ev);
            }
            else if (ev.isController())
            {
                uint8_t channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                uint8_t controller = static_cast<uint8_t>(ev.getP1());
                uint8_t value = static_cast<uint8_t>(ev.getP2());

                Event cc_ev{};
                cc_ev.tick = omega_tick;
                cc_ev.sink_id = 0u;
                cc_ev.payload_tag = OMEGA_CC;
                cc_ev.channel = channel;
                cc_ev.data[0] = controller;
                cc_ev.data[1] = value;

                engine.add_track_event(track_id, cc_ev);
            }
            else if (ev.isPatchChange())
            {
                uint8_t channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                uint8_t program = static_cast<uint8_t>(ev.getP1());

                Event prog_ev{};
                prog_ev.tick = omega_tick;
                prog_ev.sink_id = 0u;
                prog_ev.payload_tag = OMEGA_PROGRAM;
                prog_ev.channel = channel;
                prog_ev.data[0] = program;

                engine.add_track_event(track_id, prog_ev);
            }
        }
    }

    return OMEGA_OK;
}

}  // namespace omega
