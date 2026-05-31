#include <omega/anchor_point.h>
#include <omega/engine.h>
#include <omega/marker_list.h>
#include <omega/pattern.h>
#include <omega/pattern_library.h>
#include <omega/perf_slot.h>
#include <omega/region_list.h>
#include <omega/session.h>
#include <omega/smpte_converter.h>
#include <omega/song_arrangement.h>
#include <omega/tempo_map.h>
#include <omega/time_signature_map.h>
#include <omega/timeline.h>
#include <omega/track.h>
#include <omega/types.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace omega
{

// ── Binary format ──────────────────────────────────────────────────────────────
//
// All integers are little-endian.
//
//   [4] magic   = {'O','M','G','S'}
//   [4] version = 1
//   [sections…]
//   [4+4] end marker: tag=0 len=0
//
// Each section:
//   [4] tag    — see TAG_* constants
//   [4] length — payload bytes (excludes this 8-byte header)
//   [length]   — payload
//
// Unknown section tags are skipped using the length field, allowing older
// readers to open sessions written by newer writers.

static const std::array<uint8_t, 4> MAGIC = {'O', 'M', 'G', 'S'};
static constexpr uint32_t SESSION_VERSION = 1u;

static constexpr uint32_t TAG_END = 0x00000000u;
static constexpr uint32_t TAG_TEMPO = 0x4F504D54u;      // TMPO
static constexpr uint32_t TAG_TIMESIG = 0x47495354u;    // TSIG
static constexpr uint32_t TAG_SMPTE = 0x45544D53u;      // SMTE
static constexpr uint32_t TAG_LOOP = 0x504F4F4Cu;       // LOOP
static constexpr uint32_t TAG_MARKERS = 0x534B524Du;    // MRKS
static constexpr uint32_t TAG_REGIONS = 0x534E4752u;    // RGNS
static constexpr uint32_t TAG_PATTERNS = 0x534E5450u;   // PTNS
static constexpr uint32_t TAG_PERF = 0x46524550u;       // PERF
static constexpr uint32_t TAG_SONG = 0x474E4F53u;       // SONG
static constexpr uint32_t TAG_CTX = 0x58544350u;        // PCTX
static constexpr uint32_t TAG_TRACKS = 0x534B5254u;     // TRKS
static constexpr uint32_t TAG_TRANSPORT = 0x534E5254u;  // TRNS

// ── BufWriter ─────────────────────────────────────────────────────────────────

class BufWriter
{
public:
    void u8(uint8_t v) { buf_.push_back(v); }

    void u16(uint16_t v)
    {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }

    void u32(uint32_t v)
    {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
        u8(static_cast<uint8_t>(v >> 16));
        u8(static_cast<uint8_t>(v >> 24));
    }

    void u64(uint64_t v)
    {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }

    void i8(int8_t v) { u8(static_cast<uint8_t>(v)); }

    void f32(float v)
    {
        uint32_t tmp = 0u;
        std::memcpy(&tmp, &v, 4);
        u32(tmp);
    }

    void str(const std::string& s)
    {
        u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void raw(const uint8_t* data, size_t len) { buf_.insert(buf_.end(), data, data + len); }

    void evt(const Event& e)
    {
        u64(e.tick);
        u32(e.sink_id);
        u8(e.payload_tag);
        u8(e.channel);
        u8(e.reserved[0]);
        u8(e.reserved[1]);
        for (uint8_t b : e.data)
        {
            u8(b);
        }
    }

    [[nodiscard]] size_t size() const { return buf_.size(); }
    [[nodiscard]] const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

// ── BufReader ─────────────────────────────────────────────────────────────────

class BufReader
{
public:
    BufReader(const uint8_t* data, size_t len) : p_(data), end_(data + len) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] size_t remaining() const { return ok_ ? static_cast<size_t>(end_ - p_) : 0u; }
    [[nodiscard]] const uint8_t* pos() const { return p_; }

    uint8_t u8()
    {
        if (p_ >= end_)
        {
            ok_ = false;
            return 0u;
        }
        return *p_++;
    }

    uint16_t u16()
    {
        uint16_t v = u8();
        v |= static_cast<uint16_t>(u8()) << 8;
        return v;
    }

    uint32_t u32()
    {
        uint32_t v = u8();
        v |= static_cast<uint32_t>(u8()) << 8;
        v |= static_cast<uint32_t>(u8()) << 16;
        v |= static_cast<uint32_t>(u8()) << 24;
        return v;
    }

    uint64_t u64()
    {
        uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }

    int8_t i8() { return static_cast<int8_t>(u8()); }

    float f32()
    {
        uint32_t tmp = u32();
        float v = 0.0f;
        std::memcpy(&v, &tmp, 4);
        return v;
    }

    std::string str()
    {
        uint32_t len = u32();
        if (!ok_ || len > remaining())
        {
            ok_ = false;
            return {};
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::string s(reinterpret_cast<const char*>(p_), len);
        p_ += len;
        return s;
    }

    Event evt()
    {
        Event e{};
        e.tick = u64();
        e.sink_id = u32();
        e.payload_tag = u8();
        e.channel = u8();
        e.reserved[0] = u8();
        e.reserved[1] = u8();
        for (auto& b : e.data)
        {
            b = u8();
        }
        return e;
    }

    bool skip(size_t n)
    {
        if (n > remaining())
        {
            ok_ = false;
            return false;
        }
        p_ += n;
        return true;
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_{true};
};

// ── Section emit helper ───────────────────────────────────────────────────────

static void emit_section(BufWriter& out, uint32_t tag, const BufWriter& payload)
{
    out.u32(tag);
    out.u32(static_cast<uint32_t>(payload.size()));
    out.raw(payload.data().data(), payload.size());
}

// ── Section serializers ───────────────────────────────────────────────────────

static BufWriter ser_tempo(const Engine& e)
{
    BufWriter w;
    const auto& pts = e.tempo_map().points();
    w.u32(static_cast<uint32_t>(pts.size()));
    for (const auto& pt : pts)
    {
        w.u64(pt.tick);
        w.u32(pt.bpm_milli);
    }
    return w;
}

static BufWriter ser_timesig(const Engine& e)
{
    BufWriter w;
    const auto& pts = e.timesig_map().points();
    w.u32(static_cast<uint32_t>(pts.size()));
    for (const auto& pt : pts)
    {
        w.u64(pt.tick);
        w.u8(pt.numerator);
        w.u8(pt.denominator);
    }
    return w;
}

static BufWriter ser_smpte(const Engine& e)
{
    BufWriter w;
    const auto& cfg = e.smpte_config();
    if (cfg.has_value())
    {
        w.u8(1u);
        w.u8(cfg->fps);
        w.u8(cfg->drop_frame ? 1u : 0u);
        w.u8(cfg->is_2997 ? 1u : 0u);
    }
    else
    {
        w.u8(0u);
    }
    return w;
}

static BufWriter ser_loop(const Engine& e)
{
    BufWriter w;
    const auto lr = e.loop_region();
    w.u64(lr.start_tick);
    w.u64(lr.end_tick);
    w.u8(lr.enabled ? 1u : 0u);
    return w;
}

static BufWriter ser_markers(const Engine& e)
{
    BufWriter w;
    const auto& ml = e.marker_list();
    w.u32(ml.size());
    for (uint32_t i = 0; i < ml.size(); ++i)
    {
        const auto* m = ml.at(i);
        w.u64(m->tick);
        w.str(m->name);
    }
    return w;
}

static BufWriter ser_regions(const Engine& e)
{
    BufWriter w;
    const auto& rl = e.region_list();
    w.u32(rl.size());
    for (uint32_t i = 0; i < rl.size(); ++i)
    {
        const auto* r = rl.at(i);
        w.u64(r->start_tick);
        w.u64(r->end_tick);
        w.u8(static_cast<uint8_t>(r->type));
        w.str(r->name);
    }
    return w;
}

static void ser_anchor_list(BufWriter& w, const AnchorList& anchors)
{
    const auto& pts = anchors.points();
    const AnchorPoint* active = anchors.active_snap();

    w.u32(static_cast<uint32_t>(pts.size()));

    // Find active-snap index to save; UINT32_MAX encodes "none" (-1).
    uint32_t active_idx = UINT32_MAX;
    for (uint32_t i = 0; i < static_cast<uint32_t>(pts.size()); ++i)
    {
        if (&pts[i] == active)
        {
            active_idx = i;
            break;
        }
    }

    for (const auto& ap : pts)
    {
        w.str(ap.name);
        w.u64(ap.offset_ticks);
        w.u32(ap.flags);
    }
    w.u32(active_idx);
}

static BufWriter ser_patterns(const Engine& e)
{
    BufWriter w;
    w.u32(e.pattern_library().count());
    e.pattern_library().for_each([&](PatternId id, const Pattern& pat) {
        w.u32(id);
        w.str(pat.name);
        w.u64(pat.length_ticks);
        w.u32(static_cast<uint32_t>(pat.events.size()));
        for (const auto& ev : pat.events)
        {
            w.evt(ev);
        }
        ser_anchor_list(w, pat.anchors);
    });
    return w;
}

static BufWriter ser_perf(const Engine& e)
{
    BufWriter w;
    w.u32(PERF_MAX_SLOTS);
    for (uint32_t slot = 0; slot < PERF_MAX_SLOTS; ++slot)
    {
        const auto snap = e.perf_source().slot_snapshot(slot);
        w.u32(snap.assigned);
        w.i8(snap.transpose);
        w.u8(snap.velocity_scale);
        w.u8(snap.random_bias);
        w.u32(snap.repeat_count);
        w.u8(snap.muted ? 1u : 0u);
    }
    return w;
}

static BufWriter ser_song(const Engine& e)
{
    BufWriter w;
    const auto& entries = e.song_source().entries();
    w.u32(static_cast<uint32_t>(entries.size()));
    for (const auto& entry : entries)
    {
        w.u32(entry.pattern_id);
        w.u32(entry.repeat_count);
    }
    return w;
}

static BufWriter ser_ctx(Engine& e)
{
    BufWriter w;
    omega_perf_ctx_t ctx{};
    e.ctx_get(ctx);
    w.u8(ctx.scale.root);
    w.u8(ctx.scale.reserved);
    w.u16(ctx.scale.bitmask);
    w.u8(ctx.chord.root);
    w.u8(ctx.chord.type);
    for (uint8_t v : ctx.chord.voices)
    {
        w.u8(v);
    }
    w.i8(ctx.global_transpose);
    w.u8(ctx.global_velocity);
    w.u8(ctx.chaos);
    w.u8(ctx.groove_id);
    w.f32(ctx.swing);
    w.u32(ctx.random_seed);
    return w;
}

static BufWriter ser_tracks(const Engine& e)
{
    BufWriter w;
    const auto& tracks = e.timeline_source().tracks();
    w.u32(static_cast<uint32_t>(tracks.size()));
    for (const auto& t : tracks)
    {
        w.u32(t.id);
        w.str(t.name);
        w.u32(t.sink_id);
        w.u8(t.channel);
        w.u8(t.muted ? 1u : 0u);
        w.u8(t.soloed ? 1u : 0u);
        w.u32(static_cast<uint32_t>(t.events.size()));
        for (const auto& ev : t.events)
        {
            w.evt(ev);
        }
    }
    return w;
}

static BufWriter ser_transport(const Engine& e)
{
    BufWriter w;
    w.u64(e.transport_position_tick());
    return w;
}

// ── session_save ──────────────────────────────────────────────────────────────

omega_status_t session_save(Engine& engine, const char* path)
{
    if (path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    BufWriter out;

    // Header
    out.raw(MAGIC.data(), MAGIC.size());
    out.u32(SESSION_VERSION);

    emit_section(out, TAG_TEMPO, ser_tempo(engine));
    emit_section(out, TAG_TIMESIG, ser_timesig(engine));
    emit_section(out, TAG_SMPTE, ser_smpte(engine));
    emit_section(out, TAG_LOOP, ser_loop(engine));
    emit_section(out, TAG_MARKERS, ser_markers(engine));
    emit_section(out, TAG_REGIONS, ser_regions(engine));
    emit_section(out, TAG_PATTERNS, ser_patterns(engine));
    emit_section(out, TAG_PERF, ser_perf(engine));
    emit_section(out, TAG_SONG, ser_song(engine));
    emit_section(out, TAG_CTX, ser_ctx(engine));
    emit_section(out, TAG_TRACKS, ser_tracks(engine));
    emit_section(out, TAG_TRANSPORT, ser_transport(engine));

    // End marker
    out.u32(TAG_END);
    out.u32(0u);

    // Atomic write: temp file → rename
    std::string tmp_path = std::string(path) + ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        if (!f)
        {
            return OMEGA_ERR_IO;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        f.write(reinterpret_cast<const char*>(out.data().data()),
                static_cast<std::streamsize>(out.size()));
        if (!f)
        {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return OMEGA_ERR_IO;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec)
    {
        std::error_code ec2;
        std::filesystem::remove(tmp_path, ec2);
        return OMEGA_ERR_IO;
    }

    return OMEGA_OK;
}

// ── Section loaders ───────────────────────────────────────────────────────────

static bool load_tempo(BufReader& r, Engine& e)
{
    // Remove all non-origin tempo points first, then insert from file.
    // Origin (tick=0) cannot be removed — insert(0, bpm) replaces it.
    {
        std::vector<uint64_t> to_remove;
        for (const auto& pt : e.tempo_map().points())
        {
            if (pt.tick != 0)
            {
                to_remove.push_back(pt.tick);
            }
        }
        for (uint64_t tick : to_remove)
        {
            e.tempo_map().remove(tick);
        }
    }
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t tick = r.u64();
        uint32_t bpm_milli = r.u32();
        if (!r.ok())
        {
            return false;
        }
        e.tempo_map().insert(tick, bpm_milli);
    }
    return r.ok();
}

static bool load_timesig(BufReader& r, Engine& e)
{
    e.timesig_map().clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t tick = r.u64();
        uint8_t num = r.u8();
        uint8_t den = r.u8();
        if (!r.ok())
        {
            return false;
        }
        e.timesig_map().insert(tick, num, den);
    }
    return r.ok();
}

static bool load_smpte(BufReader& r, Engine& e)
{
    uint8_t has = r.u8();
    if (!r.ok())
    {
        return false;
    }
    if (has != 0u)
    {
        uint8_t fps = r.u8();
        uint8_t drop = r.u8();
        uint8_t is29 = r.u8();
        if (!r.ok())
        {
            return false;
        }
        SmpteConfig cfg;
        cfg.fps = fps;
        cfg.drop_frame = drop != 0;
        cfg.is_2997 = is29 != 0;
        // Enqueued; drained by process() at load end.
        e.smpte_config_set(cfg);
    }
    else
    {
        e.smpte_config_clear();
    }
    return r.ok();
}

static bool load_loop(BufReader& r, Engine& e)
{
    uint64_t start = r.u64();
    uint64_t end = r.u64();
    uint8_t enabled = r.u8();
    if (!r.ok())
    {
        return false;
    }
    if (start < end)
    {
        // loop_set_immediate is the direct setter for the stopped-engine case.
        e.loop_set_immediate(start, end);
        if (enabled == 0u)
        {
            // Enqueued; drained by process() at load end.
            e.loop_enable(false);
        }
    }
    // If start >= end, loop stays at engine default (disabled, 0/0).
    return r.ok();
}

static bool load_markers(BufReader& r, Engine& e)
{
    e.marker_list().clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t tick = r.u64();
        std::string name = r.str();
        if (!r.ok())
        {
            return false;
        }
        e.marker_list().add(std::move(name), tick);
    }
    return r.ok();
}

static bool load_regions(BufReader& r, Engine& e)
{
    e.region_list().clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint64_t start = r.u64();
        uint64_t end = r.u64();
        uint8_t type = r.u8();
        std::string name = r.str();
        if (!r.ok())
        {
            return false;
        }
        if (type <= static_cast<uint8_t>(RegionType::SECTION))
        {
            e.region_list().add(std::move(name), start, end, static_cast<RegionType>(type));
        }
    }
    return r.ok();
}

static bool load_anchor_list(BufReader& r, AnchorList& anchors)
{
    anchors.clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name = r.str();
        uint64_t off = r.u64();
        uint32_t flags = r.u32();
        if (!r.ok())
        {
            return false;
        }
        anchors.add(std::move(name), off, flags);
    }
    uint32_t active_idx = r.u32();  // UINT32_MAX encodes "none"
    if (!r.ok())
    {
        return false;
    }
    if (active_idx < count)
    {
        anchors.set_active_snap(active_idx);
    }
    return r.ok();
}

static bool load_patterns(BufReader& r, Engine& e, std::unordered_map<PatternId, PatternId>& id_map)
{
    e.pattern_library().clear();
    id_map.clear();

    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t old_id = r.u32();
        std::string name = r.str();
        uint64_t length = r.u64();
        if (!r.ok())
        {
            return false;
        }

        PatternId new_id = e.create_pattern(std::move(name), length);
        id_map[old_id] = new_id;

        uint32_t event_count = r.u32();
        for (uint32_t j = 0; j < event_count; ++j)
        {
            Event ev = r.evt();
            if (!r.ok())
            {
                return false;
            }
            e.pattern_add_event(new_id, ev);
        }

        Pattern* pat = e.pattern_library().get(new_id);
        if (pat != nullptr)
        {
            if (!load_anchor_list(r, pat->anchors))
            {
                return false;
            }
        }
        else
        {
            AnchorList dummy;
            if (!load_anchor_list(r, dummy))
            {
                return false;
            }
        }
    }
    return r.ok();
}

static bool load_perf(BufReader& r,
                      Engine& e,
                      const std::unordered_map<PatternId, PatternId>& id_map)
{
    uint32_t slot_count = r.u32();
    if (!r.ok())
    {
        return false;
    }

    for (uint32_t slot = 0; slot < slot_count; ++slot)
    {
        uint32_t old_assigned = r.u32();
        int8_t transpose = r.i8();
        uint8_t vel_scale = r.u8();
        uint8_t rand_bias = r.u8();
        uint32_t repeat = r.u32();
        uint8_t muted_byte = r.u8();
        if (!r.ok())
        {
            return false;
        }

        if (slot >= PERF_MAX_SLOTS)
        {
            continue;  // forward-compatibility: skip extra slots
        }

        // Direct calls to PerformanceSource methods. These are documented as
        // "timing thread only" but are safe here since the engine is stopped.
        if (old_assigned != 0)
        {
            auto it = id_map.find(old_assigned);
            if (it != id_map.end())
            {
                e.perf_source().assign(static_cast<SlotId>(slot), it->second);
            }
        }
        e.perf_source().set_transpose(static_cast<SlotId>(slot), transpose);
        e.perf_source().set_velocity_scale(static_cast<SlotId>(slot), vel_scale);
        e.perf_source().set_random_bias(static_cast<SlotId>(slot), rand_bias);
        e.perf_source().set_repeat_count(static_cast<SlotId>(slot), repeat);
        e.perf_source().set_mute(static_cast<SlotId>(slot), muted_byte != 0);
    }
    return r.ok();
}

static bool load_song(BufReader& r,
                      Engine& e,
                      const std::unordered_map<PatternId, PatternId>& id_map)
{
    // Direct call; timing-thread-only by contract, safe since engine stopped.
    e.song_source().clear();

    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t old_pid = r.u32();
        uint32_t repeats = r.u32();
        if (!r.ok())
        {
            return false;
        }
        auto it = id_map.find(old_pid);
        if (it != id_map.end())
        {
            e.song_source().append(it->second, repeats);
        }
    }
    return r.ok();
}

static bool load_ctx(BufReader& r, Engine& e)
{
    omega_scale_t scale{};
    scale.root = r.u8();
    scale.reserved = r.u8();
    scale.bitmask = r.u16();

    omega_chord_t chord{};
    chord.root = r.u8();
    chord.type = r.u8();
    for (auto& v : chord.voices)
    {
        v = r.u8();
    }

    int8_t transpose = r.i8();
    uint8_t velocity = r.u8();
    uint8_t chaos = r.u8();
    uint8_t groove_id = r.u8();
    float swing = r.f32();
    uint32_t random_seed = r.u32();
    (void)random_seed;  // not restored via the queue (no API for it)

    if (!r.ok())
    {
        return false;
    }

    // All enqueued; drained by process() at load end.
    e.ctx_set_scale(scale);
    e.ctx_set_chord(chord);
    e.ctx_set_transpose(transpose);
    e.ctx_set_velocity(velocity);
    e.ctx_set_chaos(chaos);
    e.ctx_set_groove(groove_id, swing);
    return r.ok();
}

static bool load_tracks(BufReader& r, Engine& e)
{
    e.timeline_source().clear_tracks();

    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        r.u32();  // old_id — not needed after loading
        std::string name = r.str();
        uint32_t sink_id = r.u32();
        uint8_t channel = r.u8();
        uint8_t muted = r.u8();
        uint8_t soloed = r.u8();
        if (!r.ok())
        {
            return false;
        }

        TrackId new_id = e.add_track(std::move(name));
        e.set_track_sink(new_id, sink_id);
        e.set_track_channel(new_id, channel);

        uint32_t event_count = r.u32();
        for (uint32_t j = 0; j < event_count; ++j)
        {
            Event ev = r.evt();
            if (!r.ok())
            {
                return false;
            }
            e.add_track_event(new_id, ev);
        }

        if (muted != 0u)
        {
            e.set_track_mute(new_id, true);
        }
        if (soloed != 0u)
        {
            e.set_track_solo(new_id, true);
        }
    }
    return r.ok();
}

// ── session_load ──────────────────────────────────────────────────────────────

omega_status_t session_load(Engine& engine, const char* path)
{
    if (path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    // Read entire file into memory; validate header before touching engine state.
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return OMEGA_ERR_IO;
    }
    const auto file_size = f.tellg();
    if (file_size <= 0)
    {
        return OMEGA_ERR_IO;
    }
    std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
    f.seekg(0);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    f.read(reinterpret_cast<char*>(file_data.data()), file_size);
    if (!f)
    {
        return OMEGA_ERR_IO;
    }

    BufReader r(file_data.data(), file_data.size());

    // Validate magic
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 4> magic{};
    for (auto& b : magic)
    {
        b = r.u8();
    }
    if (!r.ok() || std::memcmp(magic.data(), MAGIC.data(), MAGIC.size()) != 0)
    {
        return OMEGA_ERR_IO;
    }

    uint32_t version = r.u32();
    if (!r.ok() || version < 1)
    {
        return OMEGA_ERR_IO;
    }

    // Pattern ID mapping: old ID (from file) → new ID (assigned on load)
    std::unordered_map<PatternId, PatternId> id_map;

    // Process sections
    while (r.ok() && r.remaining() >= 8)
    {
        uint32_t tag = r.u32();
        uint32_t len = r.u32();
        if (!r.ok())
        {
            return OMEGA_ERR_IO;
        }
        if (tag == TAG_END)
        {
            break;
        }
        if (len > r.remaining())
        {
            return OMEGA_ERR_IO;
        }

        // Create a sub-reader bounded to exactly this section's payload.
        const uint8_t* section_start = r.pos();
        BufReader sr(section_start, len);
        r.skip(len);

        bool ok = true;
        switch (tag)
        {
            case TAG_TEMPO:
                ok = load_tempo(sr, engine);
                break;
            case TAG_TIMESIG:
                ok = load_timesig(sr, engine);
                break;
            case TAG_SMPTE:
                ok = load_smpte(sr, engine);
                break;
            case TAG_LOOP:
                ok = load_loop(sr, engine);
                break;
            case TAG_MARKERS:
                ok = load_markers(sr, engine);
                break;
            case TAG_REGIONS:
                ok = load_regions(sr, engine);
                break;
            case TAG_PATTERNS:
                ok = load_patterns(sr, engine, id_map);
                break;
            case TAG_PERF:
                ok = load_perf(sr, engine, id_map);
                break;
            case TAG_SONG:
                ok = load_song(sr, engine, id_map);
                break;
            case TAG_CTX:
                ok = load_ctx(sr, engine);
                break;
            case TAG_TRACKS:
                ok = load_tracks(sr, engine);
                break;
            case TAG_TRANSPORT: /* saved but not restored */
                break;
            default: /* unknown — already skipped */
                break;
        }

        if (!ok)
        {
            return OMEGA_ERR_IO;
        }
    }

    if (!r.ok())
    {
        return OMEGA_ERR_IO;
    }

    // Drain enqueued commands (smpte_config, loop_enable, ctx_set_*, track mute/solo).
    engine.process();

    return OMEGA_OK;
}

}  // namespace omega
