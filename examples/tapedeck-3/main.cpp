/*
 * tapedeck-3 — Omega (current) example
 *
 * The same 8-track, 8-bar looping MIDI sequencer, rewritten a third time to
 * demonstrate the query-boundary APIs (Q1–Q4) added after tapedeck-2.
 *
 * Workarounds eliminated vs tapedeck-2:
 *
 *   ✓ bool playing (manual transport state mirror) — eliminated.
 *     Q3: engine.set_event_callback(). g_transport_running is set to true by
 *     SPACE and cleared by the OMEGA_EVENT_TRANSPORT_STOPPED callback.
 *     The engine now tells us when it actually stops.
 *
 *   ✓ bool slot_cued (UI shadows whether the slot is already active) — eliminated.
 *     Q1: engine.perf_slot_state(0). On PLAY, cue only if the slot is EMPTY
 *     or IDLE; it is already in motion otherwise.
 *
 *   ✓ bool muted_[NUM_TRACKS] / soloed_[NUM_TRACKS] (shadow arrays) — eliminated.
 *     Q2: engine.sink_is_muted() / sink_is_soloed() return live atomic state.
 *     Toggle is a plain read-modify-write:
 *       engine.sink_set_mute(sid, ch, !engine.sink_is_muted(sid, ch))
 *     No mute/solo state is threaded through draw functions.
 *
 *   ✓ pos.loop_count for loop tracking — replaced.
 *     Q3: OMEGA_EVENT_LOOP_WRAPPED callback stores the engine's own counter in
 *     g_loop_count. No position snapshot required.
 *
 * New in tapedeck-3:
 *   • Live slot state indicator (EMPTY/IDLE/QUEUED/PLAYING/STOPPING) from Q1.
 *   • Last engine event shown in the status line from Q3.
 *
 * Controls: identical to tapedeck and tapedeck-2.
 *   UP/DOWN — Select track
 *   m       — Toggle mute on selected track
 *   s       — Toggle solo on selected track
 *   Enter   — Open track detail view
 *   SPACE   — Play / Stop
 *   ESC     — Rewind to start
 *   q       — Exit
 *
 *   (track detail)  UP/DOWN: select event  n: rename  Enter: edit event  ESC: back
 *   (note edit)     TAB: cycle field  LEFT/RIGHT: change value  Enter: save  ESC: cancel
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/midi_io.h>
#include <omega/omega.h>
#include <omega/pattern_library.h>
#include <omega/sink.h>
#include <omega/tempo_map.h>
#include <omega/timer.h>
#include <omega/types.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using namespace omega;

// ─── Session constants ────────────────────────────────────────────────────────

static constexpr uint32_t BPM = 105;
static constexpr uint32_t BPM_MILLI = BPM * 1000;
static constexpr uint32_t NUM_TRACKS = 8;
static constexpr uint32_t BARS = 8;
static constexpr uint32_t BEATS_PER_BAR = 4;
static constexpr uint64_t PATTERN_LEN =
    static_cast<uint64_t>(BARS) * BEATS_PER_BAR * PPQN;  // 15360 ticks

// ─── Callback-driven engine state ─────────────────────────────────────────────
//
// Q3: these atomics are updated by engine_event_cb(), which fires on the timing
// thread.  The UI thread reads them with relaxed ordering — any transient
// inconsistency only affects display, never correctness.

static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_transport_running{false};    // replaces bool playing
static std::atomic<uint32_t> g_loop_count{0};           // replaces pos.loop_count
static std::atomic<uint32_t> g_last_event{UINT32_MAX};  // last omega_engine_event_t

// Q3: timing-thread callback.  Must not block, allocate, or call back into the engine.
static void engine_event_cb(omega_engine_event_t event, uint32_t detail, void* /*userdata*/)
{
    g_last_event.store(static_cast<uint32_t>(event), std::memory_order_relaxed);
    switch (event)
    {
        case OMEGA_EVENT_TRANSPORT_STOPPED:
            g_transport_running.store(false, std::memory_order_relaxed);
            break;
        case OMEGA_EVENT_LOOP_WRAPPED:
            // detail == engine's loop_count after the wrap.
            g_loop_count.store(detail, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

static const char* event_name(uint32_t e)
{
    switch (e)
    {
        case OMEGA_EVENT_SLOT_STARTED:
            return "SLOT_STARTED";
        case OMEGA_EVENT_SLOT_STOPPED:
            return "SLOT_STOPPED";
        case OMEGA_EVENT_LOOP_WRAPPED:
            return "LOOP_WRAPPED";
        case OMEGA_EVENT_TRANSPORT_STOPPED:
            return "TRANSPORT_STOPPED";
        default:
            return "(none)";
    }
}

// ─── Terminal raw mode ────────────────────────────────────────────────────────

static struct termios g_orig_termios;

static void restore_terminal()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
}

static void setup_terminal()
{
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(restore_terminal);

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~static_cast<unsigned>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= static_cast<unsigned>(CS8);
    raw.c_lflag &= ~static_cast<unsigned>(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// ─── Input ────────────────────────────────────────────────────────────────────

static constexpr int KEY_NONE = 0;
static constexpr int KEY_UP = 256;
static constexpr int KEY_DOWN = 257;
static constexpr int KEY_LEFT = 258;
static constexpr int KEY_RIGHT = 259;
static constexpr int KEY_ENTER = 13;
static constexpr int KEY_TAB = 9;
static constexpr int KEY_BACKSPACE = 127;

static int read_input()
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return KEY_NONE;

    if (c != '\x1b')
        return static_cast<unsigned char>(c);

    struct timeval tv = {0, 10000};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return '\x1b';

    char seq[4] = {};
    ssize_t nr = read(STDIN_FILENO, seq, sizeof(seq));
    if (nr >= 2 && seq[0] == '[')
    {
        if (seq[1] == 'A')
            return KEY_UP;
        if (seq[1] == 'B')
            return KEY_DOWN;
        if (seq[1] == 'C')
            return KEY_RIGHT;
        if (seq[1] == 'D')
            return KEY_LEFT;
    }
    return KEY_NONE;
}

// ─── ActivitySink ─────────────────────────────────────────────────────────────
//
// Wraps LibremidiSink to record per-channel note-on timestamps for activity
// LEDs.  Mute/solo filtering is handled by the engine's FilteringDispatcher
// (added in tapedeck-2); suppressed events never reach send().

class ActivitySink : public OutputSink
{
public:
    static constexpr uint64_t ACTIVE_NS = 150'000'000ULL;

    explicit ActivitySink(LibremidiSink& inner) noexcept : inner_(inner) {}

    void send(const Event& e) override
    {
        if (e.channel < 16 && e.payload_tag == OMEGA_NOTE_ON)
            last_on_[e.channel].store(now_ns(), std::memory_order_relaxed);
        inner_.send(e);
    }

    void flush() override { inner_.flush(); }

    [[nodiscard]] bool channel_active(uint8_t ch) const noexcept
    {
        if (ch >= 16)
            return false;
        uint64_t t = last_on_[ch].load(std::memory_order_relaxed);
        return t > 0 && (now_ns() - t) < ACTIVE_NS;
    }

    [[nodiscard]] bool any_active() const noexcept
    {
        for (uint8_t ch = 0; ch < 16; ++ch)
        {
            if (channel_active(ch))
                return true;
        }
        return false;
    }

    [[nodiscard]] bool port_open() const noexcept { return inner_.is_port_open(); }

private:
    static uint64_t now_ns() noexcept
    {
        return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    LibremidiSink& inner_;
    std::array<std::atomic<uint64_t>, 16> last_on_{};
};

// ─── Track metadata ───────────────────────────────────────────────────────────

struct TrackMeta
{
    char name[32];
    bool has_content;
};

static TrackMeta TRACKS[NUM_TRACKS] = {
    {"Bass", true},
    {"Pad", true},
    {"Melody", true},
    {"Accent", true},
    {"(empty)", false},
    {"(empty)", false},
    {"(empty)", false},
    {"(empty)", false},
};

// ─── Pattern content ──────────────────────────────────────────────────────────

static void add_note(Engine& eng,
                     PatternId pid,
                     uint32_t sid,
                     uint64_t tick,
                     uint8_t ch,
                     uint8_t note,
                     uint8_t vel,
                     uint32_t dur)
{
    eng.pattern_add_event(pid, omega_make_note_on(tick, sid, ch, note, vel, dur));
}

static void populate_pattern(Engine& eng, PatternId pid, uint32_t sid)
{
    const uint64_t BAR = BEATS_PER_BAR * PPQN;
    const uint64_t BEAT = PPQN;
    const uint64_t E8 = PPQN / 2;

    // Track 0 — Bass
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        auto qn = static_cast<uint32_t>(BEAT - 40);
        auto en = static_cast<uint32_t>(E8 - 20);
        if (b % 2 == 0)
        {
            add_note(eng, pid, sid, o + 0 * BEAT, 0, 36, 100, qn);
            add_note(eng, pid, sid, o + 1 * BEAT, 0, 43, 80, en);
            add_note(eng, pid, sid, o + 1 * BEAT + E8, 0, 39, 72, en);
            add_note(eng, pid, sid, o + 2 * BEAT, 0, 36, 90, qn);
            add_note(eng, pid, sid, o + 3 * BEAT, 0, 34, 85, qn);
        }
        else
        {
            add_note(eng, pid, sid, o + 0 * BEAT, 0, 41, 100, qn);
            add_note(eng, pid, sid, o + 1 * BEAT, 0, 43, 80, en);
            add_note(eng, pid, sid, o + 1 * BEAT + E8, 0, 41, 72, en);
            add_note(eng, pid, sid, o + 2 * BEAT, 0, 43, 92, qn);
            add_note(eng, pid, sid, o + 3 * BEAT, 0, 36, 85, qn);
        }
    }

    // Track 1 — Pad
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        auto dur = static_cast<uint32_t>(BAR - 60);
        if ((b / 2) % 2 == 0)
        {
            add_note(eng, pid, sid, o, 1, 48, 65, dur);
            add_note(eng, pid, sid, o, 1, 51, 60, dur);
            add_note(eng, pid, sid, o, 1, 55, 55, dur);
        }
        else
        {
            add_note(eng, pid, sid, o, 1, 53, 65, dur);
            add_note(eng, pid, sid, o, 1, 56, 60, dur);
            add_note(eng, pid, sid, o, 1, 60, 55, dur);
        }
    }

    // Track 2 — Melody
    static const uint8_t PHRASE_A[8] = {60, 63, 65, 67, 65, 63, 60, 58};
    static const uint8_t PHRASE_B[8] = {58, 60, 63, 65, 67, 65, 63, 60};
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        const uint8_t* ph = (b % 2 == 0) ? PHRASE_A : PHRASE_B;
        for (int i = 0; i < 8; ++i)
        {
            add_note(eng,
                     pid,
                     sid,
                     o + static_cast<uint64_t>(i) * E8,
                     2,
                     ph[i],
                     88,
                     static_cast<uint32_t>(E8 - 20));
        }
    }

    // Track 3 — Accent
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        add_note(eng, pid, sid, o + 0 * BEAT, 3, 67, 92, static_cast<uint32_t>(E8));
        add_note(eng, pid, sid, o + 2 * BEAT, 3, 65, 80, static_cast<uint32_t>(E8));
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void format_tick_pos(uint64_t tick, char* out, size_t out_size)
{
    const uint64_t ticks_per_bar = BEATS_PER_BAR * PPQN;
    uint64_t bar = tick / ticks_per_bar + 1;
    uint64_t beat = (tick % ticks_per_bar) / PPQN + 1;
    uint64_t sub = tick % PPQN;
    if (sub == 0)
        snprintf(out, out_size, "%llu:%llu", (unsigned long long)bar, (unsigned long long)beat);
    else
        snprintf(out,
                 out_size,
                 "%llu:%llu.%llu",
                 (unsigned long long)bar,
                 (unsigned long long)beat,
                 (unsigned long long)sub);
}

// Count note-on events for the given channel.
// Thread safety: the timing thread reads pattern events read-only; all edits
// go through pattern_replace_event() and are applied between process() cycles.
// This iteration is safe at any time.
static int count_channel_events(const Engine& engine, PatternId pat_id, uint32_t track)
{
    const Pattern* pat = engine.pattern_library().get(pat_id);
    if (pat == nullptr)
        return 0;
    int n = 0;
    for (const auto& e : pat->events)
    {
        if (e.channel == static_cast<uint8_t>(track) && e.payload_tag == OMEGA_NOTE_ON)
            ++n;
    }
    return n;
}

// Q1: slot state as a short display label.
static const char* slot_state_label(SlotState s)
{
    switch (s)
    {
        case SlotState::QUEUED:
            return "QUEUED  ";
        case SlotState::PLAYING:
            return "PLAYING ";
        case SlotState::STOPPING:
            return "STOPPING";
        case SlotState::IDLE:
            return "IDLE    ";
        default:
            return "EMPTY   ";
    }
}

// ─── Display: main screen ─────────────────────────────────────────────────────
//
// Q1: slot state from engine.perf_slot_state() — no shadow variable.
// Q2: mute/solo from engine.sink_is_muted() / sink_is_soloed() — no arrays.
// Q3: loop_count and transport state from g_loop_count / g_transport_running.

static void draw_main(const Engine& eng,
                      const ActivitySink& activity,
                      uint32_t sid,
                      uint32_t selected_track)
{
    // ✓ Q1: live slot state — no bool slot_cued needed.
    SlotState slot = eng.perf_slot_state(0);

    // ✓ Q3: transport state and loop count driven by callbacks.
    bool playing = g_transport_running.load(std::memory_order_relaxed);
    uint32_t loop_count = g_loop_count.load(std::memory_order_relaxed);

    // Bar/beat from the atomic position snapshot.
    omega_position_t pos = eng.position();
    uint32_t bar = (pos.bar > 0) ? pos.bar : 1;
    uint32_t beat = (pos.beat > 0) ? pos.beat : 1;

    uint64_t pos_ns = eng.transport_position_ns();
    uint64_t total_s = pos_ns / 1'000'000'000ULL;
    char time_str[16];
    snprintf(time_str,
             sizeof(time_str),
             "%llu:%02llu.%llu",
             (unsigned long long)(total_s / 60),
             (unsigned long long)(total_s % 60),
             (unsigned long long)((pos_ns % 1'000'000'000ULL) / 100'000'000ULL));

    // Bar progress indicator.
    char bar_ind[BARS + 1];
    bar_ind[BARS] = '\0';
    for (uint32_t b = 0; b < BARS; ++b)
    {
        if (b < bar - 1)
            bar_ind[b] = '#';
        else if (b == bar - 1)
            bar_ind[b] = '>';
        else
            bar_ind[b] = '.';
    }

    // 32-beat progress bar.
    char prog[33];
    prog[32] = '\0';
    int cur_beat = static_cast<int>((bar - 1) * BEATS_PER_BAR + (beat - 1));
    for (int i = 0; i < 32; ++i)
    {
        if (i < cur_beat)
            prog[i] = '#';
        else if (i == cur_beat)
            prog[i] = '>';
        else
            prog[i] = '.';
    }

    bool midi_any = activity.any_active();
    bool midi_ok = activity.port_open();

    // ✓ Q2: any_solo computed from engine queries — no soloed_[] array.
    bool any_solo = false;
    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
        any_solo = any_solo || eng.sink_is_soloed(sid, static_cast<uint8_t>(t));

    printf("\033[H");
    printf("=================================================================\n");
    printf("  OMEGA TAPEDECK-3    %u BPM  |  4/4  |  8 Bars  [%s]\n", BPM, slot_state_label(slot));
    printf("=================================================================\n");
    printf("  Trk  Name        [12345678]   Ch   Mut  Sol   MIDI\n");
    printf("  ---  ----------  ----------   --   ---  ---   ----\n");

    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
    {
        bool selected = (t == selected_track);

        // ✓ Q2: live mute/solo from engine — no shadow arrays.
        bool muted = eng.sink_is_muted(sid, static_cast<uint8_t>(t));
        bool soloed = eng.sink_is_soloed(sid, static_cast<uint8_t>(t));
        bool silent = muted || (any_solo && !soloed);

        const char* mstr = muted ? "[M]" : "[ ]";
        const char* sstr = soloed ? "[S]" : "[ ]";
        const char* act =
            (!silent && activity.channel_active(static_cast<uint8_t>(t))) ? "[*]" : "[ ]";

        if (selected)
            printf("\033[7m");

        if (TRACKS[t].has_content)
            printf("   %u   %-10s  [%s]    %u   %s  %s   %s",
                   t + 1,
                   TRACKS[t].name,
                   bar_ind,
                   t + 1,
                   mstr,
                   sstr,
                   act);
        else
            printf("   %u   %-10s  [........]   %u   %s  %s   %s",
                   t + 1,
                   TRACKS[t].name,
                   t + 1,
                   mstr,
                   sstr,
                   act);

        if (selected)
            printf("\033[0m");
        printf("\n");
    }

    printf("-----------------------------------------------------------------\n");
    printf("  [%s]\n", prog);
    // ✓ Q3: loop_count from OMEGA_EVENT_LOOP_WRAPPED callback, not pos.loop_count.
    printf("  Bar %u/8   Beat %u/4   Loop: %u   Time: %s\n", bar, beat, loop_count, time_str);
    // ✓ Q3: transport state from g_transport_running; last engine event from g_last_event.
    printf("  %-14s  MIDI: %s %-11s  Last: %s\n",
           playing ? "PLAYING >>>" : "STOPPED [=]",
           midi_any ? "[*]" : "[ ]",
           midi_ok ? "connected" : "no port",
           event_name(g_last_event.load(std::memory_order_relaxed)));
    printf("=================================================================\n");
    printf("  UP/DOWN: Select   m: Mute   s: Solo   Enter: Edit track\n");
    printf("  SPACE: Play/Stop   ESC: Rewind   q: Quit\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Display: track detail screen ────────────────────────────────────────────
//
// Q2: mute/solo read from engine — is_muted/is_soloed no longer passed in.

static constexpr int EDIT_VISIBLE = 16;

static void draw_track_edit(const Engine& engine,
                            PatternId pat_id,
                            uint32_t track,
                            uint32_t sid,
                            int scroll_offset,
                            int event_cursor)
{
    // ✓ Q2: live mute/solo — no caller-supplied booleans needed.
    auto ch = static_cast<uint8_t>(track);
    bool is_muted = engine.sink_is_muted(sid, ch);
    bool is_soloed = engine.sink_is_soloed(sid, ch);

    const Pattern* pat = engine.pattern_library().get(pat_id);

    int total = 0;
    if (pat != nullptr)
    {
        for (const auto& e : pat->events)
        {
            if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
                ++total;
        }
    }

    int offset = std::min(scroll_offset, std::max(0, total - EDIT_VISIBLE));

    printf("\033[H");
    printf("=================================================================\n");
    printf("  Track %u -- %-10s  Ch %u   Mute: %s   Solo: %s\n",
           track + 1,
           TRACKS[track].name,
           track + 1,
           is_muted ? "[M]" : "[ ]",
           is_soloed ? "[S]" : "[ ]");
    printf("  Pattern: %-20s  Events: %d\n", pat != nullptr ? pat->name.c_str() : "(none)", total);
    printf("=================================================================\n");
    printf("    #    Tick     Position    Note     Vel   Dur (ticks)\n");
    printf("  ---   ------   ----------  ------   ---   -----------\n");

    if (total == 0 || pat == nullptr)
    {
        printf("  (no events on this channel)\n");
        for (int i = 1; i < EDIT_VISIBLE; ++i)
            printf("\n");
    }
    else
    {
        int event_num = 0;
        int shown = 0;
        for (const auto& e : pat->events)
        {
            if (e.channel != ch || e.payload_tag != OMEGA_NOTE_ON)
                continue;

            bool selected = (event_num == event_cursor);
            ++event_num;

            if (event_num - 1 < offset)
                continue;
            if (shown >= EDIT_VISIBLE)
                break;

            uint32_t dur = omega_event_note_duration(&e);
            char note_str[8];
            omega_midi_note_name(e.data[0], note_str, sizeof(note_str));
            char pos_str[32];
            format_tick_pos(e.tick, pos_str, sizeof(pos_str));

            if (selected)
                printf("\033[7m");
            printf("  %3d   %6llu   %-10s  %-6s   %3u   %u",
                   event_num,
                   (unsigned long long)e.tick,
                   pos_str,
                   note_str,
                   omega_event_note_velocity(&e),
                   dur);
            if (selected)
                printf("\033[0m");
            printf("\n");
            ++shown;
        }
        for (int i = shown; i < EDIT_VISIBLE; ++i)
            printf("\n");
    }

    int first = (total > 0) ? offset + 1 : 0;
    int last_shown = (total > 0) ? std::min(offset + EDIT_VISIBLE, total) : 0;
    printf("-----------------------------------------------------------------\n");
    printf("  Showing %d-%d of %d events\n", first, last_shown, total);
    printf("=================================================================\n");
    printf("  UP/DOWN: Select event   n: Edit name   Enter: Edit event\n");
    printf("  ESC: Back\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Display: name edit screen ────────────────────────────────────────────────

static void draw_name_edit(uint32_t track, const char* name_buf)
{
    printf("\033[H");
    printf("=================================================================\n");
    printf("  Edit Track Name -- Track %u\n", track + 1);
    printf("=================================================================\n");
    printf("\n");
    printf("  Current name:  %s\n", TRACKS[track].name);
    printf("\n");
    printf("  New name:      %s_\n", name_buf);
    printf("\n");
    printf("  Type to edit, Backspace to delete\n");
    printf("=================================================================\n");
    printf("  Enter: Save   ESC: Cancel\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Display: note edit screen ────────────────────────────────────────────────

static void draw_note_edit(uint32_t track, int event_num, const Event& ev, int field)
{
    uint32_t dur = omega_event_note_duration(&ev);
    char note_str[8];
    omega_midi_note_name(omega_event_note_pitch(&ev), note_str, sizeof(note_str));
    char pos_str[32];
    format_tick_pos(ev.tick, pos_str, sizeof(pos_str));

    printf("\033[H");
    printf("=================================================================\n");
    printf("  Edit Event -- Track %u: %s   Event %d   Pos: %s\n",
           track + 1,
           TRACKS[track].name,
           event_num,
           pos_str);
    printf("=================================================================\n");
    printf("\n");
    printf("  Changes apply live: pattern_replace_event() enqueues via SPSC queue.\n");
    printf("\n");
    printf("     Field       Value      LEFT / RIGHT\n");
    printf("  ---------    --------    ----------------------------\n");

    const char* s0 = (field == 0) ? "\033[7m" : "";
    const char* e0 = (field == 0) ? "\033[0m" : "";
    const char* s1 = (field == 1) ? "\033[7m" : "";
    const char* e1 = (field == 1) ? "\033[0m" : "";
    const char* s2 = (field == 2) ? "\033[7m" : "";
    const char* e2 = (field == 2) ? "\033[0m" : "";

    printf("  %s  Note        %-8s   +/-1 semitone (0-127)%s\n", s0, note_str, e0);
    printf("  %s  Velocity    %-8u   +/-1 (1-127)%s\n", s1, omega_event_note_velocity(&ev), e1);
    printf("  %s  Duration    %-8u   +/-120 ticks (~1/4 beat)%s\n", s2, dur, e2);

    printf("\n");
    printf("=================================================================\n");
    printf("  TAB: Next field   Enter: Save   ESC: Cancel\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

enum class Screen
{
    MAIN,
    TRACK_EDIT,
    NAME_EDIT,
    NOTE_EDIT
};

int main()
{
    setup_terminal();

    Engine engine;
    LibremidiSink midi_sink(nullptr);
    ActivitySink activity(midi_sink);
    engine.add_sink(&activity);
    const uint32_t sid = activity.sink_id();

    engine.tempo_map().insert(0, BPM_MILLI);
    engine.timesig_map().insert(0, BEATS_PER_BAR, 4);

    PatternId pat = engine.create_pattern("8-bar loop", PATTERN_LEN);
    populate_pattern(engine, pat, sid);
    engine.enqueue(PerfAssignCmd{0, pat});

    // loop_set() enables the loop region; loop_enable(true) is not needed.
    engine.loop_set(0, PATTERN_LEN);

    // ✓ Q3: register the callback before the timer thread starts.
    engine.set_event_callback(engine_event_cb, nullptr);

    OmegaTimer timer(engine, 1000);

    // ── UI state ─────────────────────────────────────────────────────────────
    //
    // Compare to tapedeck-2's equivalent block — these variables are gone:
    //   bool playing        → g_transport_running (updated by callback)
    //   bool slot_cued      → engine.perf_slot_state(0) (Q1 query)
    //   bool muted_[8]      → engine.sink_is_muted()    (Q2 query)
    //   bool soloed_[8]     → engine.sink_is_soloed()   (Q2 query)

    Screen screen = Screen::MAIN;
    uint32_t selected_track = 0;
    int event_cursor = 0;
    int scroll_offset = 0;
    int edit_field = 0;
    int edit_raw_idx = -1;
    Event edit_event = {};
    int edit_event_num = 0;
    char name_buf[32] = {};

    printf("\033[2J");
    draw_main(engine, activity, sid, selected_track);

    while (!g_quit.load(std::memory_order_relaxed))
    {
        int key = read_input();

        if (screen == Screen::NAME_EDIT)
        {
            if (key == '\x1b')
            {
                screen = Screen::TRACK_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_ENTER)
            {
                std::strncpy(TRACKS[selected_track].name, name_buf, 31);
                TRACKS[selected_track].name[31] = '\0';
                screen = Screen::TRACK_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_BACKSPACE || key == 8)
            {
                int len = static_cast<int>(std::strlen(name_buf));
                if (len > 0)
                    name_buf[len - 1] = '\0';
            }
            else if (key >= 32 && key < 127)
            {
                int len = static_cast<int>(std::strlen(name_buf));
                if (len < 31)
                {
                    name_buf[len] = static_cast<char>(key);
                    name_buf[len + 1] = '\0';
                }
            }
        }
        else if (screen == Screen::NOTE_EDIT)
        {
            if (key == '\x1b')
            {
                screen = Screen::TRACK_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_ENTER)
            {
                // pattern_replace_event() enqueues the change through the SPSC
                // command queue — safe even while the transport is running.
                if (edit_raw_idx >= 0)
                    engine.pattern_replace_event(
                        pat, static_cast<uint32_t>(edit_raw_idx), edit_event);
                screen = Screen::TRACK_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_TAB)
            {
                edit_field = (edit_field + 1) % 3;
            }
            else if (key == KEY_LEFT)
            {
                if (edit_field == 0)
                {
                    uint8_t p = omega_event_note_pitch(&edit_event);
                    if (p > 0)
                        omega_event_set_pitch(&edit_event, static_cast<uint8_t>(p - 1));
                }
                else if (edit_field == 1)
                {
                    uint8_t v = omega_event_note_velocity(&edit_event);
                    if (v > 1)
                        omega_event_set_velocity(&edit_event, static_cast<uint8_t>(v - 1));
                }
                else
                {
                    uint32_t d = omega_event_note_duration(&edit_event);
                    if (d > 120)
                        omega_event_set_duration(&edit_event, d - 120);
                }
            }
            else if (key == KEY_RIGHT)
            {
                if (edit_field == 0)
                {
                    uint8_t p = omega_event_note_pitch(&edit_event);
                    if (p < 127)
                        omega_event_set_pitch(&edit_event, static_cast<uint8_t>(p + 1));
                }
                else if (edit_field == 1)
                {
                    uint8_t v = omega_event_note_velocity(&edit_event);
                    if (v < 127)
                        omega_event_set_velocity(&edit_event, static_cast<uint8_t>(v + 1));
                }
                else
                {
                    uint32_t d = omega_event_note_duration(&edit_event);
                    omega_event_set_duration(&edit_event, d + 120);
                }
            }
        }
        else if (screen == Screen::TRACK_EDIT)
        {
            if (key == '\x1b')
            {
                screen = Screen::MAIN;
                printf("\033[2J");
            }
            else if (key == 'n' || key == 'N')
            {
                std::strncpy(name_buf, TRACKS[selected_track].name, 31);
                name_buf[31] = '\0';
                screen = Screen::NAME_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_UP)
            {
                if (event_cursor > 0)
                    event_cursor--;
            }
            else if (key == KEY_DOWN)
            {
                int total = count_channel_events(engine, pat, selected_track);
                if (event_cursor < total - 1)
                    event_cursor++;
            }
            else if (key == KEY_ENTER)
            {
                const Pattern* ppat = engine.pattern_library().get(pat);
                if (ppat != nullptr)
                {
                    auto ch = static_cast<uint8_t>(selected_track);
                    int count = 0;
                    for (int i = 0; i < static_cast<int>(ppat->events.size()); ++i)
                    {
                        const auto& e = ppat->events[static_cast<size_t>(i)];
                        if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
                        {
                            if (count == event_cursor)
                            {
                                edit_raw_idx = i;
                                edit_event = e;
                                edit_event_num = count + 1;
                                edit_field = 0;
                                screen = Screen::NOTE_EDIT;
                                printf("\033[2J");
                                break;
                            }
                            ++count;
                        }
                    }
                }
            }

            if (event_cursor < scroll_offset)
                scroll_offset = event_cursor;
            else if (event_cursor >= scroll_offset + EDIT_VISIBLE)
                scroll_offset = event_cursor - EDIT_VISIBLE + 1;
        }
        else  // Screen::MAIN
        {
            switch (key)
            {
                case 'q':
                case 'Q':
                    g_quit.store(true, std::memory_order_relaxed);
                    break;

                case KEY_ENTER:
                    event_cursor = 0;
                    scroll_offset = 0;
                    screen = Screen::TRACK_EDIT;
                    printf("\033[2J");
                    break;

                case ' ':
                    if (!g_transport_running.load(std::memory_order_relaxed))
                    {
                        // ✓ Q3: set optimistically; OMEGA_EVENT_TRANSPORT_STOPPED clears it.
                        g_transport_running.store(true, std::memory_order_relaxed);
                        engine.enqueue(TransportCmd{TransportAction::PLAY, 0});
                        // ✓ Q1: cue only if the slot is not already active.
                        SlotState ss = engine.perf_slot_state(0);
                        if (ss == SlotState::EMPTY || ss == SlotState::IDLE)
                            engine.enqueue(PerfCueCmd{0, CueMode::IMMEDIATE});
                    }
                    else
                    {
                        engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                        // g_transport_running cleared by OMEGA_EVENT_TRANSPORT_STOPPED callback.
                    }
                    break;

                case '\x1b':
                    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                    engine.enqueue(TransportCmd{TransportAction::LOCATE, 0});
                    // LOCATE resets the engine's loop_count_ to 0 but fires no callback;
                    // mirror that reset here.
                    g_loop_count.store(0, std::memory_order_relaxed);
                    break;

                case KEY_UP:
                    selected_track = (selected_track + NUM_TRACKS - 1) % NUM_TRACKS;
                    break;

                case KEY_DOWN:
                    selected_track = (selected_track + 1) % NUM_TRACKS;
                    break;

                case 'm':
                case 'M':
                {
                    // ✓ Q2: read-modify-write — no muted_[] array.
                    auto ch = static_cast<uint8_t>(selected_track);
                    engine.sink_set_mute(sid, ch, !engine.sink_is_muted(sid, ch));
                    break;
                }

                case 's':
                case 'S':
                {
                    // ✓ Q2: read-modify-write — no soloed_[] array.
                    auto ch = static_cast<uint8_t>(selected_track);
                    engine.sink_set_solo(sid, ch, !engine.sink_is_soloed(sid, ch));
                    break;
                }

                default:
                    break;
            }
        }

        // ── Redraw ───────────────────────────────────────────────────────────
        switch (screen)
        {
            case Screen::MAIN:
                draw_main(engine, activity, sid, selected_track);
                break;
            case Screen::TRACK_EDIT:
                draw_track_edit(engine, pat, selected_track, sid, scroll_offset, event_cursor);
                break;
            case Screen::NAME_EDIT:
                draw_name_edit(selected_track, name_buf);
                break;
            case Screen::NOTE_EDIT:
                draw_note_edit(selected_track, edit_event_num, edit_event, edit_field);
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
    restore_terminal();
    printf("\n\033[2J\033[H");
    printf("Omega Tapedeck-3 stopped.\n");
    return 0;
}
