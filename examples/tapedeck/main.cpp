/*
 * tapedeck — Omega v1.0.0 example
 *
 * An 8-track, 8-bar looping MIDI sequencer with a tapedeck-style transport.
 * All 8 tracks output to the same MIDI port on channels 1-8.
 *
 * Main screen:
 *   UP / DOWN   — Select track
 *   m           — Toggle mute on selected track
 *   s           — Toggle solo on selected track
 *   Enter       — Open track detail view
 *   SPACE       — Play / Stop
 *   ESC         — Rewind to start (stops if playing)
 *   q           — Exit
 *
 * Track detail screen:
 *   UP / DOWN   — Select event
 *   n           — Edit track name
 *   Enter       — Edit selected event
 *   ESC         — Back to main
 *
 * Name edit screen:
 *   Type        — Enter characters
 *   Backspace   — Delete last character
 *   Enter       — Save
 *   ESC         — Cancel
 *
 * Note edit screen:
 *   TAB         — Cycle field (Note → Velocity → Duration)
 *   LEFT / RIGHT — Decrease / increase current field
 *   Enter       — Save
 *   ESC         — Cancel
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
#include <cmath>
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

// ─── Quit flag ────────────────────────────────────────────────────────────────

static std::atomic<bool> g_quit{false};

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
    raw.c_cc[VMIN] = 0;  // non-blocking reads
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
    {
        return KEY_NONE;
    }

    if (c != '\x1b')
    {
        return static_cast<unsigned char>(c);
    }

    struct timeval tv = {0, 10000};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
    {
        return '\x1b';
    }

    char seq[4] = {};
    ssize_t nr = read(STDIN_FILENO, seq, sizeof(seq));
    if (nr >= 2 && seq[0] == '[')
    {
        if (seq[1] == 'A')
        {
            return KEY_UP;
        }
        if (seq[1] == 'B')
        {
            return KEY_DOWN;
        }
        if (seq[1] == 'C')
        {
            return KEY_RIGHT;
        }
        if (seq[1] == 'D')
        {
            return KEY_LEFT;
        }
    }
    return KEY_NONE;
}

// ─── Activity-tracking sink ───────────────────────────────────────────────────

class ActivitySink : public OutputSink
{
public:
    static constexpr uint64_t ACTIVE_NS = 150'000'000ULL;

    explicit ActivitySink(LibremidiSink& inner) noexcept : inner_(inner) {}

    void send(const Event& e) override
    {
        if (e.channel < 16 && e.payload_tag == OMEGA_NOTE_ON)
        {
            bool muted = muted_[e.channel].load(std::memory_order_relaxed);
            bool any_solo = any_soloed_.load(std::memory_order_relaxed);
            bool soloed = soloed_[e.channel].load(std::memory_order_relaxed);
            if (muted || (any_solo && !soloed))
            {
                return;
            }
            last_on_[e.channel].store(now_ns(), std::memory_order_relaxed);
        }
        inner_.send(e);
    }

    void flush() override { inner_.flush(); }

    void toggle_mute(uint8_t ch) noexcept
    {
        if (ch >= 16)
        {
            return;
        }
        bool prev = muted_[ch].load(std::memory_order_relaxed);
        muted_[ch].store(!prev, std::memory_order_relaxed);
    }

    void toggle_solo(uint8_t ch) noexcept
    {
        if (ch >= 16)
        {
            return;
        }
        bool prev = soloed_[ch].load(std::memory_order_relaxed);
        soloed_[ch].store(!prev, std::memory_order_relaxed);
        bool any = false;
        for (uint8_t i = 0; i < 16; ++i)
        {
            if (soloed_[i].load(std::memory_order_relaxed))
            {
                any = true;
                break;
            }
        }
        any_soloed_.store(any, std::memory_order_relaxed);
    }

    [[nodiscard]] bool is_muted(uint8_t ch) const noexcept
    {
        return ch < 16 && muted_[ch].load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool is_soloed(uint8_t ch) const noexcept
    {
        return ch < 16 && soloed_[ch].load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool channel_active(uint8_t ch) const noexcept
    {
        if (ch >= 16)
        {
            return false;
        }
        uint64_t t = last_on_[ch].load(std::memory_order_relaxed);
        return t > 0 && (now_ns() - t) < ACTIVE_NS;
    }

    [[nodiscard]] bool any_active() const noexcept
    {
        for (uint8_t ch = 0; ch < 16; ++ch)
        {
            if (channel_active(ch))
            {
                return true;
            }
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
    std::array<std::atomic<bool>, 16> muted_{};
    std::array<std::atomic<bool>, 16> soloed_{};
    std::atomic<bool> any_soloed_{false};
};

// ─── Track metadata ───────────────────────────────────────────────────────────

struct TrackMeta
{
    char name[32];
    bool has_content;
};

// Mutable so names can be edited at runtime.
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
    Event ev = omega_make_note_on(tick, sid, ch, note, vel, dur);
    eng.pattern_add_event(pid, ev);
}

static void populate_pattern(Engine& eng, PatternId pid, uint32_t sid)
{
    const uint64_t BAR = BEATS_PER_BAR * PPQN;
    const uint64_t BEAT = PPQN;
    const uint64_t E8 = PPQN / 2;

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

    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        add_note(eng, pid, sid, o + 0 * BEAT, 3, 67, 92, static_cast<uint32_t>(E8));
        add_note(eng, pid, sid, o + 2 * BEAT, 3, 65, 80, static_cast<uint32_t>(E8));
    }
}

// ─── Note / position helpers ──────────────────────────────────────────────────

static const char* const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static void format_note(uint8_t midi_note, char* out, size_t out_size)
{
    int octave = static_cast<int>(midi_note / 12) - 1;
    snprintf(out, out_size, "%s%d", NOTE_NAMES[midi_note % 12], octave);
}

static void format_pos(uint64_t tick, char* out, size_t out_size)
{
    const uint64_t ticks_per_bar = BEATS_PER_BAR * PPQN;
    uint64_t bar = tick / ticks_per_bar + 1;
    uint64_t beat = (tick % ticks_per_bar) / PPQN + 1;
    uint64_t sub = tick % PPQN;

    if (sub == 0)
    {
        snprintf(out, out_size, "%llu:%llu", (unsigned long long)bar, (unsigned long long)beat);
    }
    else
    {
        snprintf(out,
                 out_size,
                 "%llu:%llu.%llu",
                 (unsigned long long)bar,
                 (unsigned long long)beat,
                 (unsigned long long)sub);
    }
}

// Count note-on events for a given MIDI channel in the pattern.
static int count_channel_events(const Engine& engine, PatternId pat_id, uint32_t track)
{
    const Pattern* pat = engine.pattern_library().get(pat_id);
    if (pat == nullptr)
    {
        return 0;
    }
    int n = 0;
    auto ch = static_cast<uint8_t>(track);
    for (const auto& e : pat->events)
    {
        if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
        {
            ++n;
        }
    }
    return n;
}

// ─── Display: main screen ─────────────────────────────────────────────────────

static void draw_main(const Engine& eng,
                      const ActivitySink& activity,
                      bool playing,
                      uint32_t loop_count,
                      uint32_t selected_track)
{
    uint64_t pos_ns = eng.transport_position_ns();
    double pos_sec = static_cast<double>(pos_ns) / 1.0e9;
    double beats = pos_sec * BPM / 60.0;
    auto pat_len = static_cast<double>(BARS * BEATS_PER_BAR);
    double beat_in = fmod(beats, pat_len);

    auto bar = static_cast<uint32_t>(beat_in / BEATS_PER_BAR) + 1;
    auto beat = static_cast<uint32_t>(fmod(beat_in, BEATS_PER_BAR)) + 1;

    uint64_t total_s = pos_ns / 1'000'000'000ULL;
    char time_str[16];
    snprintf(time_str,
             sizeof(time_str),
             "%llu:%02llu.%llu",
             (unsigned long long)(total_s / 60),
             (unsigned long long)(total_s % 60),
             (unsigned long long)((pos_ns % 1'000'000'000ULL) / 100'000'000ULL));

    char bar_ind[9];
    bar_ind[8] = '\0';
    for (uint32_t b = 0; b < BARS; ++b)
    {
        if (b < bar - 1)
        {
            bar_ind[b] = '#';
        }
        else if (b == bar - 1)
        {
            bar_ind[b] = '>';
        }
        else
        {
            bar_ind[b] = '.';
        }
    }

    char prog[33];
    prog[32] = '\0';
    auto cur_beat = static_cast<int>((bar - 1) * BEATS_PER_BAR + (beat - 1));
    for (int i = 0; i < 32; ++i)
    {
        if (i < cur_beat)
        {
            prog[i] = '#';
        }
        else if (i == cur_beat)
        {
            prog[i] = '>';
        }
        else
        {
            prog[i] = '.';
        }
    }

    bool midi_any = activity.any_active();
    bool midi_ok = activity.port_open();
    bool any_solo = false;
    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
    {
        if (activity.is_soloed(static_cast<uint8_t>(t)))
        {
            any_solo = true;
            break;
        }
    }

    printf("\033[H");
    printf("=================================================================\n");
    printf("  OMEGA TAPEDECK  v1.0.0    %u BPM  |  4/4  |  8 Bars\n", BPM);
    printf("=================================================================\n");
    printf("  Trk  Name        [12345678]   Ch   Mut  Sol   MIDI\n");
    printf("  ---  ----------  ----------   --   ---  ---   ----\n");

    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
    {
        bool selected = (t == selected_track);
        bool muted = activity.is_muted(static_cast<uint8_t>(t));
        bool soloed = activity.is_soloed(static_cast<uint8_t>(t));
        bool silent = muted || (any_solo && !soloed);

        const char* mstr = muted ? "[M]" : "[ ]";
        const char* sstr = soloed ? "[S]" : "[ ]";
        const char* act =
            (!silent && activity.channel_active(static_cast<uint8_t>(t))) ? "[*]" : "[ ]";

        if (selected)
        {
            printf("\033[7m");
        }

        if (TRACKS[t].has_content)
        {
            printf("   %u   %-10s  [%s]    %u   %s  %s   %s",
                   t + 1,
                   TRACKS[t].name,
                   bar_ind,
                   t + 1,
                   mstr,
                   sstr,
                   act);
        }
        else
        {
            printf("   %u   %-10s  [........]   %u   %s  %s   %s",
                   t + 1,
                   TRACKS[t].name,
                   t + 1,
                   mstr,
                   sstr,
                   act);
        }

        if (selected)
        {
            printf("\033[0m");
        }
        printf("\n");
    }

    printf("-----------------------------------------------------------------\n");
    printf("  [%s]\n", prog);
    printf("  Bar %u/8   Beat %u/4   Loop: %u   Time: %s\n", bar, beat, loop_count, time_str);
    printf("  %-14s  MIDI: %s %s\n",
           playing ? "PLAYING >>>" : "STOPPED [=]",
           midi_any ? "[*]" : "[ ]",
           midi_ok ? "connected" : "no port");
    printf("=================================================================\n");
    printf("  UP/DOWN: Select   m: Mute   s: Solo   Enter: Edit track\n");
    printf("  SPACE: Play/Stop   ESC: Rewind   q: Quit\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Display: track detail screen ────────────────────────────────────────────

static constexpr int EDIT_VISIBLE = 16;

static void draw_track_edit(const Engine& engine,
                            const ActivitySink& activity,
                            PatternId pat_id,
                            uint32_t track,
                            int scroll_offset,
                            int event_cursor)
{
    const Pattern* pat = engine.pattern_library().get(pat_id);
    auto ch = static_cast<uint8_t>(track);

    int total = 0;
    if (pat != nullptr)
    {
        for (const auto& e : pat->events)
        {
            if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
            {
                ++total;
            }
        }
    }

    int max_offset = std::max(0, total - EDIT_VISIBLE);
    int offset = std::min(scroll_offset, max_offset);

    printf("\033[H");
    printf("=================================================================\n");
    printf("  Track %u — %-10s  Ch %u   Mute: %s   Solo: %s\n",
           track + 1,
           TRACKS[track].name,
           track + 1,
           activity.is_muted(ch) ? "[M]" : "[ ]",
           activity.is_soloed(ch) ? "[S]" : "[ ]");
    printf("  Pattern: %-20s  Events: %d\n", pat != nullptr ? pat->name.c_str() : "(none)", total);
    printf("=================================================================\n");
    printf("    #    Tick     Position    Note     Vel   Dur (ticks)\n");
    printf("  ---   ------   ----------  ------   ---   -----------\n");

    if (total == 0 || pat == nullptr)
    {
        printf("  (no events on this channel)\n");
        for (int i = 1; i < EDIT_VISIBLE; ++i)
        {
            printf("\n");
        }
    }
    else
    {
        int event_num = 0;
        int shown = 0;
        for (const auto& e : pat->events)
        {
            if (e.channel != ch || e.payload_tag != OMEGA_NOTE_ON)
            {
                continue;
            }

            bool selected = (event_num == event_cursor);
            ++event_num;

            if (event_num - 1 < offset)
            {
                continue;
            }
            if (shown >= EDIT_VISIBLE)
            {
                break;
            }

            uint32_t dur = 0;
            std::memcpy(&dur, e.data + 2, sizeof(dur));

            char note_str[8];
            format_note(e.data[0], note_str, sizeof(note_str));

            char pos_str[32];
            format_pos(e.tick, pos_str, sizeof(pos_str));

            if (selected)
            {
                printf("\033[7m");
            }

            printf("  %3d   %6llu   %-10s  %-6s   %3u   %u",
                   event_num,
                   (unsigned long long)e.tick,
                   pos_str,
                   note_str,
                   e.data[1],
                   dur);

            if (selected)
            {
                printf("\033[0m");
            }

            printf("\n");
            ++shown;
        }

        for (int i = shown; i < EDIT_VISIBLE; ++i)
        {
            printf("\n");
        }
    }

    int first = (total > 0) ? offset + 1 : 0;
    int last_shown = (total > 0) ? std::min(offset + EDIT_VISIBLE, total) : 0;
    printf("-----------------------------------------------------------------\n");
    printf("  Showing %d–%d of %d events\n", first, last_shown, total);
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
    printf("  Edit Track Name — Track %u\n", track + 1);
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
    uint32_t dur = 0;
    std::memcpy(&dur, ev.data + 2, sizeof(dur));

    char note_str[8];
    format_note(ev.data[0], note_str, sizeof(note_str));

    char pos_str[32];
    format_pos(ev.tick, pos_str, sizeof(pos_str));

    printf("\033[H");
    printf("=================================================================\n");
    printf("  Edit Event — Track %u: %s   Event %d   Pos: %s\n",
           track + 1,
           TRACKS[track].name,
           event_num,
           pos_str);
    printf("=================================================================\n");
    printf("\n");
    printf("  Playback stopped. Press SPACE to resume when done.\n");
    printf("\n");
    printf("     Field       Value      LEFT / RIGHT\n");
    printf("  ---------    --------    ----------------------------\n");

    // Each row: show `>>` for the selected field, ` ` otherwise.
    const char* s0 = (field == 0) ? "\033[7m" : "";
    const char* e0 = (field == 0) ? "\033[0m" : "";
    const char* s1 = (field == 1) ? "\033[7m" : "";
    const char* e1 = (field == 1) ? "\033[0m" : "";
    const char* s2 = (field == 2) ? "\033[7m" : "";
    const char* e2 = (field == 2) ? "\033[0m" : "";

    printf("  %s  Note        %-8s   ±1 semitone (0–127)%s\n", s0, note_str, e0);
    printf("  %s  Velocity    %-8u   ±1 (1–127)%s\n", s1, ev.data[1], e1);
    printf("  %s  Duration    %-8u   ±120 ticks (~1/4 beat)%s\n", s2, dur, e2);

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

    OmegaTimer timer(engine, 1000);

    // ── UI state ─────────────────────────────────────────────────────────────
    Screen screen = Screen::MAIN;
    bool playing = false;
    bool slot_cued = false;
    uint32_t loop_count = 0;
    uint64_t last_loop_idx = 0;
    uint32_t selected_track = 0;
    int event_cursor = 0;
    int scroll_offset = 0;
    int edit_field = 0;
    int edit_raw_idx = -1;
    Event edit_event = {};
    int edit_event_num = 0;  // 1-based, for display
    char name_buf[32] = {};

    printf("\033[2J");
    draw_main(engine, activity, playing, loop_count, selected_track);

    while (!g_quit.load(std::memory_order_relaxed))
    {
        int key = read_input();

        // ── Screen-specific key handling ──────────────────────────────────────
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
                {
                    name_buf[len - 1] = '\0';
                }
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
                // Apply the edit. Transport is already stopped; timing thread
                // is not advancing the pattern, so direct write is safe.
                Pattern* ppat = engine.pattern_library().get(pat);
                if (ppat != nullptr && edit_raw_idx >= 0 &&
                    edit_raw_idx < static_cast<int>(ppat->events.size()))
                {
                    ppat->events[static_cast<size_t>(edit_raw_idx)] = edit_event;
                }
                screen = Screen::TRACK_EDIT;
                printf("\033[2J");
            }
            else if (key == KEY_TAB)
            {
                edit_field = (edit_field + 1) % 3;
            }
            else if (key == KEY_LEFT)
            {
                if (edit_field == 0 && edit_event.data[0] > 0)
                {
                    edit_event.data[0]--;
                }
                else if (edit_field == 1 && edit_event.data[1] > 1)
                {
                    edit_event.data[1]--;
                }
                else if (edit_field == 2)
                {
                    uint32_t dur = 0;
                    std::memcpy(&dur, edit_event.data + 2, sizeof(dur));
                    if (dur > 120)
                    {
                        dur -= 120;
                        std::memcpy(edit_event.data + 2, &dur, sizeof(dur));
                    }
                }
            }
            else if (key == KEY_RIGHT)
            {
                if (edit_field == 0 && edit_event.data[0] < 127)
                {
                    edit_event.data[0]++;
                }
                else if (edit_field == 1 && edit_event.data[1] < 127)
                {
                    edit_event.data[1]++;
                }
                else if (edit_field == 2)
                {
                    uint32_t dur = 0;
                    std::memcpy(&dur, edit_event.data + 2, sizeof(dur));
                    dur += 120;
                    std::memcpy(edit_event.data + 2, &dur, sizeof(dur));
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
                {
                    event_cursor--;
                }
            }
            else if (key == KEY_DOWN)
            {
                int total = count_channel_events(engine, pat, selected_track);
                if (event_cursor < total - 1)
                {
                    event_cursor++;
                }
            }
            else if (key == KEY_ENTER)
            {
                // Find the raw vector index for the selected event.
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
                                // Stop playback so the timing thread won't read
                                // the pattern events while we edit.
                                if (playing)
                                {
                                    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                                    playing = false;
                                }
                                screen = Screen::NOTE_EDIT;
                                printf("\033[2J");
                                break;
                            }
                            ++count;
                        }
                    }
                }
            }

            // Scroll to keep event_cursor visible.
            if (event_cursor < scroll_offset)
            {
                scroll_offset = event_cursor;
            }
            else if (event_cursor >= scroll_offset + EDIT_VISIBLE)
            {
                scroll_offset = event_cursor - EDIT_VISIBLE + 1;
            }
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
                    if (!playing)
                    {
                        engine.enqueue(TransportCmd{TransportAction::PLAY, 0});
                        if (!slot_cued)
                        {
                            engine.enqueue(PerfCueCmd{0, CueMode::IMMEDIATE});
                            slot_cued = true;
                        }
                        playing = true;
                    }
                    else
                    {
                        engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                        playing = false;
                    }
                    break;

                case '\x1b':
                    if (playing)
                    {
                        engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                        playing = false;
                    }
                    engine.enqueue(TransportCmd{TransportAction::LOCATE, 0});
                    slot_cued = false;
                    loop_count = 0;
                    last_loop_idx = 0;
                    break;

                case KEY_UP:
                    selected_track = (selected_track + NUM_TRACKS - 1) % NUM_TRACKS;
                    break;

                case KEY_DOWN:
                    selected_track = (selected_track + 1) % NUM_TRACKS;
                    break;

                case 'm':
                case 'M':
                    activity.toggle_mute(static_cast<uint8_t>(selected_track));
                    break;

                case 's':
                case 'S':
                    activity.toggle_solo(static_cast<uint8_t>(selected_track));
                    break;

                default:
                    break;
            }
        }

        // Loop-wrap counter (main screen transport only).
        if (playing)
        {
            uint64_t pos_ns = engine.transport_position_ns();
            double pos_beats = static_cast<double>(pos_ns) / 1.0e9 * BPM / 60.0;
            auto idx = static_cast<uint64_t>(pos_beats) / (BARS * BEATS_PER_BAR);
            if (idx > last_loop_idx)
            {
                loop_count += static_cast<uint32_t>(idx - last_loop_idx);
                last_loop_idx = idx;
            }
        }

        switch (screen)
        {
            case Screen::MAIN:
                draw_main(engine, activity, playing, loop_count, selected_track);
                break;
            case Screen::TRACK_EDIT:
                draw_track_edit(engine, activity, pat, selected_track, scroll_offset, event_cursor);
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
    printf("Omega Tapedeck stopped.\n");
    return 0;
}
