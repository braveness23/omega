#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/smf.h>
#include <omega/types.h>

#include <array>
#include <cstring>
#include <optional>
#include <string>

#include "MidiFile.h"

namespace omega
{

namespace
{

// Per-SMF-track helper that lazily creates omega tracks and routes them.
//
// In non-split mode there is at most one omega track per SMF track; its channel
// is taken from the first musical event written. In split mode there is one
// omega track per MIDI channel, created on first use, each stamped with its
// channel. Either way the track (and the caller's events) route to opts.sink_id.
class TrackFactory
{
public:
    TrackFactory(Engine& engine, int smf_track, const SmfImportOptions& opts) noexcept
        : engine_(engine), smf_track_(smf_track), opts_(opts)
    {}

    // Returns the omega TrackId that should receive an event on `channel`,
    // creating and routing the track on first use.
    TrackId track_for(uint8_t channel)
    {
        if (opts_.split_by_channel)
        {
            auto& slot = channel_tracks_[channel & 0x0Fu];
            if (!slot)
            {
                TrackId id = engine_.add_track("track_" + std::to_string(smf_track_) + "_ch" +
                                               std::to_string((channel & 0x0Fu) + 1u));
                engine_.set_track_channel(id, static_cast<uint8_t>(channel & 0x0Fu));
                engine_.set_track_sink(id, opts_.sink_id);
                slot = id;
            }
            return *slot;
        }

        if (!single_track_)
        {
            TrackId id = engine_.add_track("track_" + std::to_string(smf_track_));
            engine_.set_track_channel(id, static_cast<uint8_t>(channel & 0x0Fu));
            engine_.set_track_sink(id, opts_.sink_id);
            single_track_ = id;
        }
        return *single_track_;
    }

private:
    Engine& engine_;
    int smf_track_;
    const SmfImportOptions& opts_;
    std::optional<TrackId> single_track_;
    std::array<std::optional<TrackId>, 16> channel_tracks_{};
};

}  // namespace

omega_status_t smf_import(Engine& engine, const char* path, const SmfImportOptions& opts)
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
        // Tracks are created lazily on the first musical event so that tracks
        // carrying only meta events (tempo, time sig, markers) do not appear as
        // empty tracks in the engine.
        TrackFactory factory{engine, t, opts};

        for (int e = 0; e < mf[t].size(); ++e)
        {
            const smf::MidiEvent& ev = mf[t][e];
            // Scale SMF ticks to omega ticks: omega_tick = smf_tick * OMEGA_PPQN / smf_ppqn.
            // This is a linear interpolation between resolutions. Integer division truncates,
            // which is acceptable — the rounding error is sub-tick at any musical tempo.
            uint64_t omega_tick =
                (static_cast<uint64_t>(ev.tick) * static_cast<uint64_t>(OMEGA_PPQN)) /
                static_cast<uint64_t>(smf_ppqn);

            if (ev.isTempo())
            {
                int usec = ev.getTempoMicroseconds();
                if (usec > 0)
                {
                    auto bpm_milli =
                        static_cast<uint32_t>(60'000'000'000ULL / static_cast<uint64_t>(usec));
                    engine.tempo_map().insert(omega_tick, bpm_milli);
                }
            }
            else if (ev.isTimeSignature())
            {
                // FF 58 04 <num> <denom-log2> <clocks> <32nds>
                if (ev.size() == 7)
                {
                    auto num = static_cast<uint8_t>(ev[3]);
                    auto denom = static_cast<uint8_t>(1 << ev[4]);
                    engine.timesig_map().insert(omega_tick, num, denom);
                }
            }
            else if (ev.isMeta())
            {
                int meta_type = ev.getMetaType();
                // 0x06 = Marker (FF 06) — a named position marker in the SMF.
                // 0x07 = Cue Point (FF 07) — like a marker but intended for synchronisation cues.
                // Both are imported as omega markers; cue points get a "[cue] " prefix.
                if (meta_type == 0x06 || meta_type == 0x07)
                {
                    std::string name = ev.getMetaContent();
                    if (meta_type == 0x07)
                    {
                        name.insert(0, "[cue] ");
                    }
                    engine.marker_list().add(std::move(name), omega_tick);
                }
            }
            else if (ev.isNoteOn())
            {
                // The low nibble of the MIDI status byte (ev[0]) holds the channel (0–15).
                auto channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                auto note = static_cast<uint8_t>(ev.getP1());
                auto velocity = static_cast<uint8_t>(ev.getP2());

                // getTickDuration() returns the SMF tick distance to the linked note-off
                // (populated by mf.linkNotePairs()). Apply the same PPQN scaling as for
                // note ticks so the duration is expressed in omega ticks.
                uint32_t duration = 0u;
                if (ev.isLinked() != 0)
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
                note_ev.sink_id = opts.sink_id;
                note_ev.payload_tag = OMEGA_NOTE_ON;
                note_ev.channel = channel;
                note_ev.data[0] = note;
                note_ev.data[1] = velocity;
                std::memcpy(&note_ev.data[2], &duration, sizeof(duration));

                engine.add_track_event(factory.track_for(channel), note_ev);
            }
            else if (ev.isController())
            {
                auto channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                auto controller = static_cast<uint8_t>(ev.getP1());
                auto value = static_cast<uint8_t>(ev.getP2());

                Event cc_ev{};
                cc_ev.tick = omega_tick;
                cc_ev.sink_id = opts.sink_id;
                cc_ev.payload_tag = OMEGA_CC;
                cc_ev.channel = channel;
                cc_ev.data[0] = controller;
                cc_ev.data[1] = value;

                engine.add_track_event(factory.track_for(channel), cc_ev);
            }
            else if (ev.isPatchChange())
            {
                auto channel = static_cast<uint8_t>(ev[0] & 0x0Fu);
                auto program = static_cast<uint8_t>(ev.getP1());

                Event prog_ev{};
                prog_ev.tick = omega_tick;
                prog_ev.sink_id = opts.sink_id;
                prog_ev.payload_tag = OMEGA_PROGRAM;
                prog_ev.channel = channel;
                prog_ev.data[0] = program;

                engine.add_track_event(factory.track_for(channel), prog_ev);
            }
        }
    }

    return OMEGA_OK;
}

omega_status_t smf_import(Engine& engine, const char* path)
{
    return smf_import(engine, path, SmfImportOptions{});
}

}  // namespace omega
