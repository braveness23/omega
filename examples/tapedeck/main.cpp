/*
 * tapedeck — Omega v1.0.0 example
 *
 * An 8-track, 8-bar looping MIDI sequencer with a tapedeck-style transport.
 * All 8 tracks output to the same MIDI port on channels 1-8.
 *
 * Main screen controls:
 *   UP / DOWN   — Select track
 *   m           — Toggle mute on selected track
 *   s           — Toggle solo on selected track
 *   Enter       — Open track detail view
 *   SPACE       — Play / Stop
 *   ESC         — Rewind to start (stops if playing)
 *   q           — Exit
 *
 * Track detail controls:
 *   UP / DOWN   — Scroll event list
 *   ESC         — Back to main screen
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
static constexpr int KEY_ENTER = 13;

// Read one key (or escape sequence) non-blocking.
// Returns KEY_NONE if no input, KEY_UP/KEY_DOWN for arrow keys,
// KEY_ENTER for Return, '\x1b' for bare ESC, or the ASCII value otherwise.
static int read_input()
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return KEY_NONE;

    if (c != '\x1b')
        return static_cast<unsigned char>(c);

    // Peek for an escape sequence within 10 ms.
    struct timeval tv = {0, 10000};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return '\x1b';  // bare ESC

    char seq[4] = {};
    ssize_t nr = read(STDIN_FILENO, seq, sizeof(seq));
    if (nr >= 2 && seq[0] == '[')
    {
        if (seq[1] == 'A')
            return KEY_UP;
        if (seq[1] == 'B')
            return KEY_DOWN;
    }
    return KEY_NONE;  // unknown escape sequence
}

// ─── Activity-tracking sink ───────────────────────────────────────────────────
//
// Wraps LibremidiSink, records the timestamp of the most recent note-on per
// MIDI channel, and enforces per-channel mute/solo. Note-ons for muted or
// non-soloed channels are suppressed; note-offs and CC always pass through so
// that in-flight notes release cleanly.
//
// Called from both the timing thread (send) and the display/mutation thread
// (toggle_mute, toggle_solo, channel_active). All shared state is atomic.

class ActivitySink : public OutputSink
{
public:
    static constexpr uint64_t ACTIVE_NS = 150'000'000ULL;  // 150 ms hold time

    explicit ActivitySink(LibremidiSink& inner) noexcept : inner_(inner) {}

    void send(const Event& e) override
    {
        if (e.channel < 16 && e.payload_tag == OMEGA_NOTE_ON)
        {
            bool muted = muted_[e.channel].load(std::memory_order_relaxed);
            bool any_solo = any_soloed_.load(std::memory_order_relaxed);
            bool soloed = soloed_[e.channel].load(std::memory_order_relaxed);
            if (muted || (any_solo && !soloed))
                return;
            last_on_[e.channel].store(now_ns(), std::memory_order_relaxed);
        }
        inner_.send(e);
    }

    void flush() override { inner_.flush(); }

    void toggle_mute(uint8_t ch) noexcept
    {
        if (ch >= 16)
            return;
        bool prev = muted_[ch].load(std::memory_order_relaxed);
        muted_[ch].store(!prev, std::memory_order_relaxed);
    }

    void toggle_solo(uint8_t ch) noexcept
    {
        if (ch >= 16)
            return;
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

    bool is_muted(uint8_t ch) const noexcept
    {
        return ch < 16 && muted_[ch].load(std::memory_order_relaxed);
    }

    bool is_soloed(uint8_t ch) const noexcept
    {
        return ch < 16 && soloed_[ch].load(std::memory_order_relaxed);
    }

    bool channel_active(uint8_t ch) const noexcept
    {
        if (ch >= 16)
            return false;
        uint64_t t = last_on_[ch].load(std::memory_order_relaxed);
        return t > 0 && (now_ns() - t) < ACTIVE_NS;
    }

    bool any_active() const noexcept
    {
        for (uint8_t ch = 0; ch < 16; ++ch)
            if (channel_active(ch))
                return true;
        return false;
    }

    bool port_open() const noexcept { return inner_.is_port_open(); }

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

// ─── Pattern content ──────────────────────────────────────────────────────────

struct TrackMeta
{
    const char* name;
    bool has_content;
};

static constexpr TrackMeta TRACKS[NUM_TRACKS] = {
    {"Bass", true},
    {"Pad", true},
    {"Melody", true},
    {"Accent", true},
    {"(empty)", false},
    {"(empty)", false},
    {"(empty)", false},
    {"(empty)", false},
};

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
    const uint64_t BAR = BEATS_PER_BAR * PPQN;  // 1920
    const uint64_t BEAT = PPQN;                 // 480
    const uint64_t E8 = PPQN / 2;               // 240

    // ── Ch 0: Bass (C minor, 2-bar phrase × 4) ───────────────────────────
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        uint32_t qn = static_cast<uint32_t>(BEAT - 40);
        uint32_t en = static_cast<uint32_t>(E8 - 20);

        if (b % 2 == 0)
        {
            // Cm root feel: C2 – G2 – Eb2 – C2 – Bb1
            add_note(eng, pid, sid, o + 0 * BEAT, 0, 36, 100, qn);
            add_note(eng, pid, sid, o + 1 * BEAT, 0, 43, 80, en);
            add_note(eng, pid, sid, o + 1 * BEAT + E8, 0, 39, 72, en);
            add_note(eng, pid, sid, o + 2 * BEAT, 0, 36, 90, qn);
            add_note(eng, pid, sid, o + 3 * BEAT, 0, 34, 85, qn);
        }
        else
        {
            // Fm / G walk: F2 – G2 – F2 – G2 – C2
            add_note(eng, pid, sid, o + 0 * BEAT, 0, 41, 100, qn);
            add_note(eng, pid, sid, o + 1 * BEAT, 0, 43, 80, en);
            add_note(eng, pid, sid, o + 1 * BEAT + E8, 0, 41, 72, en);
            add_note(eng, pid, sid, o + 2 * BEAT, 0, 43, 92, qn);
            add_note(eng, pid, sid, o + 3 * BEAT, 0, 36, 85, qn);
        }
    }

    // ── Ch 1: Pad chords (whole-bar sustains, alternating Cm / Fm) ───────
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        uint32_t dur = static_cast<uint32_t>(BAR - 60);

        if ((b / 2) % 2 == 0)
        {
            // Cm = C3 Eb3 G3
            add_note(eng, pid, sid, o, 1, 48, 65, dur);
            add_note(eng, pid, sid, o, 1, 51, 60, dur);
            add_note(eng, pid, sid, o, 1, 55, 55, dur);
        }
        else
        {
            // Fm = F3 Ab3 C4
            add_note(eng, pid, sid, o, 1, 53, 65, dur);
            add_note(eng, pid, sid, o, 1, 56, 60, dur);
            add_note(eng, pid, sid, o, 1, 60, 55, dur);
        }
    }

    // ── Ch 2: Lead melody (C minor pentatonic, 8 eighth-notes per bar) ───
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

    // ── Ch 3: Accent (G4 / F4 on beats 1 & 3) ────────────────────────────
    for (uint32_t b = 0; b < BARS; ++b)
    {
        uint64_t o = b * BAR;
        add_note(eng, pid, sid, o + 0 * BEAT, 3, 67, 92, static_cast<uint32_t>(E8));
        add_note(eng, pid, sid, o + 2 * BEAT, 3, 65, 80, static_cast<uint32_t>(E8));
    }

    // Channels 4-7 are left empty.
}

// ─── Note naming ─────────────────────────────────────────────────────────────

static const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Returns e.g. "C4", "G#2", "A#-1"
static void format_note(uint8_t midi_note, char* out, int out_size)
{
    int octave = static_cast<int>(midi_note / 12) - 1;
    snprintf(out, static_cast<size_t>(out_size), "%s%d", NOTE_NAMES[midi_note % 12], octave);
}

// Returns e.g. "1:2" (bar 1, beat 2) or "1:2.240" if sub-beat ticks exist.
static void format_pos(uint64_t tick, char* out, int out_size)
{
    const uint64_t ticks_per_bar = BEATS_PER_BAR * PPQN;
    uint64_t bar = tick / ticks_per_bar + 1;
    uint64_t beat = (tick % ticks_per_bar) / PPQN + 1;
    uint64_t sub = tick % PPQN;

    if (sub == 0)
        snprintf(out,
                 static_cast<size_t>(out_size),
                 "%llu:%llu",
                 (unsigned long long)bar,
                 (unsigned long long)beat);
    else
        snprintf(out,
                 static_cast<size_t>(out_size),
                 "%llu:%llu.%llu",
                 (unsigned long long)bar,
                 (unsigned long long)beat,
                 (unsigned long long)sub);
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
    double pat_len = static_cast<double>(BARS * BEATS_PER_BAR);
    double beat_in = fmod(beats, pat_len);

    auto bar = static_cast<uint32_t>(beat_in / BEATS_PER_BAR) + 1;
    auto beat = static_cast<uint32_t>(fmod(beat_in, BEATS_PER_BAR)) + 1;

    uint64_t total_s = pos_ns / 1'000'000'000ULL;
    uint64_t mins = total_s / 60;
    uint64_t secs = total_s % 60;
    uint64_t tenths = (pos_ns % 1'000'000'000ULL) / 100'000'000ULL;
    char time_str[16];
    snprintf(time_str,
             sizeof(time_str),
             "%llu:%02llu.%llu",
             (unsigned long long)mins,
             (unsigned long long)secs,
             (unsigned long long)tenths);

    char bar_ind[9];
    bar_ind[8] = '\0';
    for (uint32_t b = 0; b < BARS; ++b)
        bar_ind[b] = (b < bar - 1) ? '#' : (b == bar - 1 ? '>' : '.');

    char prog[33];
    prog[32] = '\0';
    auto cur_beat = static_cast<int>((bar - 1) * BEATS_PER_BAR + (beat - 1));
    for (int i = 0; i < 32; ++i)
        prog[i] = (i < cur_beat) ? '#' : (i == cur_beat ? '>' : '.');

    bool midi_any = activity.any_active();
    bool midi_ok = activity.port_open();
    bool any_solo = false;
    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
        if (activity.is_soloed(static_cast<uint8_t>(t)))
        {
            any_solo = true;
            break;
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

// ─── Display: track edit screen ───────────────────────────────────────────────

static constexpr int EDIT_VISIBLE = 16;  // event rows shown at once

static void draw_track_edit(const Engine& engine,
                            const ActivitySink& activity,
                            PatternId pat_id,
                            uint32_t track,
                            int scroll_offset)
{
    const Pattern* pat = engine.pattern_library().get(pat_id);

    // Count events for this channel.
    auto ch = static_cast<uint8_t>(track);
    int total = 0;
    if (pat)
        for (const auto& e : pat->events)
            if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
                ++total;

    printf("\033[H");
    printf("=================================================================\n");
    printf("  Track %u — %-10s  Ch %u   Mute: %s   Solo: %s\n",
           track + 1,
           TRACKS[track].name,
           track + 1,
           activity.is_muted(ch) ? "[M]" : "[ ]",
           activity.is_soloed(ch) ? "[S]" : "[ ]");
    printf("  Pattern: %-20s  Events: %d\n", pat ? pat->name.c_str() : "(none)", total);
    printf("=================================================================\n");
    printf("    #    Tick     Position    Note     Vel   Dur (ticks)\n");
    printf("  ---   ------   ----------  ------   ---   -----------\n");

    // Clamp scroll so we never show an empty page when events exist.
    int max_offset = std::max(0, total - EDIT_VISIBLE);
    int offset = std::min(scroll_offset, max_offset);

    if (total == 0 || !pat)
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

            ++event_num;
            if (event_num <= offset)
                continue;
            if (shown >= EDIT_VISIBLE)
                break;

            uint32_t dur = 0;
            std::memcpy(&dur, e.data + 2, sizeof(dur));

            char note_str[8];
            format_note(e.data[0], note_str, sizeof(note_str));

            char pos_str[32];
            format_pos(e.tick, pos_str, sizeof(pos_str));

            printf("  %3d   %6llu   %-10s  %-6s   %3u   %u\n",
                   event_num,
                   (unsigned long long)e.tick,
                   pos_str,
                   note_str,
                   e.data[1],
                   dur);
            ++shown;
        }

        for (int i = shown; i < EDIT_VISIBLE; ++i)
            printf("\n");
    }

    int first = (total > 0) ? offset + 1 : 0;
    int last = (total > 0) ? std::min(offset + EDIT_VISIBLE, total) : 0;
    printf("-----------------------------------------------------------------\n");
    printf("  Showing %d–%d of %d events\n", first, last, total);
    printf("=================================================================\n");
    printf("  UP/DOWN: Scroll   ESC: Back\n");
    printf("=================================================================\n");
    fflush(stdout);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

enum class Screen
{
    MAIN,
    TRACK_EDIT
};

int main()
{
    setup_terminal();

    // ── Engine & MIDI sinks ──────────────────────────────────────────────────
    Engine engine;

    LibremidiSink midi_sink(nullptr);
    ActivitySink activity(midi_sink);
    engine.add_sink(&activity);
    const uint32_t sid = activity.sink_id();

    // ── Tempo & time signature (direct, before timer) ────────────────────────
    engine.tempo_map().insert(0, BPM_MILLI);
    engine.timesig_map().insert(0, BEATS_PER_BAR, 4);

    // ── Build the 8-bar pattern ──────────────────────────────────────────────
    PatternId pat = engine.create_pattern("8-bar loop", PATTERN_LEN);
    populate_pattern(engine, pat, sid);

    engine.enqueue(PerfAssignCmd{0, pat});

    // ── Start timing thread ──────────────────────────────────────────────────
    OmegaTimer timer(engine, 1000);

    // ── UI state ─────────────────────────────────────────────────────────────
    Screen screen = Screen::MAIN;
    bool playing = false;
    bool slot_cued = false;
    uint32_t loop_count = 0;
    uint64_t last_loop_idx = 0;
    uint32_t selected_track = 0;
    int scroll_offset = 0;

    printf("\033[2J");
    draw_main(engine, activity, playing, loop_count, selected_track);

    // ── Main loop ────────────────────────────────────────────────────────────
    while (!g_quit.load(std::memory_order_relaxed))
    {
        int key = read_input();

        if (screen == Screen::TRACK_EDIT)
        {
            if (key == '\x1b')
            {
                screen = Screen::MAIN;
                printf("\033[2J");
            }
            else if (key == KEY_UP)
            {
                scroll_offset = std::max(0, scroll_offset - 1);
            }
            else if (key == KEY_DOWN)
            {
                ++scroll_offset;  // draw_track_edit clamps to valid range
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
                    screen = Screen::TRACK_EDIT;
                    scroll_offset = 0;
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

        // Loop-wrap detection (main screen only).
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

        if (screen == Screen::TRACK_EDIT)
            draw_track_edit(engine, activity, pat, selected_track, scroll_offset);
        else
            draw_main(engine, activity, playing, loop_count, selected_track);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    engine.enqueue(TransportCmd{TransportAction::STOP, 0});

    restore_terminal();
    printf("\n\033[2J\033[H");
    printf("Omega Tapedeck stopped.\n");
    return 0;
}
