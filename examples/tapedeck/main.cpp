/*
 * tapedeck — Omega v1.0.0 example
 *
 * An 8-track, 8-bar looping MIDI sequencer with a tapedeck-style transport.
 * All 8 tracks output to the same MIDI port on channels 1-8.
 *
 * Controls:
 *   SPACE    — Play / Stop
 *   ESC      — Rewind to start (stops if playing)
 *   Ctrl+C   — Exit
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/midi_io.h>
#include <omega/omega.h>
#include <omega/sink.h>
#include <omega/tempo_map.h>
#include <omega/timer.h>
#include <omega/types.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
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

// ─── Signal handling ──────────────────────────────────────────────────────────

static std::atomic<bool> g_quit{false};

static void on_sigint(int)
{
    g_quit.store(true, std::memory_order_relaxed);
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
    raw.c_cc[VMIN] = 0;  // non-blocking reads
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Read one byte non-blocking; returns 0 if no data available.
static char read_key()
{
    char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? c : 0;
}

// Drain up to 4 pending bytes (used to swallow escape sequences).
static void drain_escape()
{
    struct timeval tv = {0, 10000};  // 10 ms
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0)
    {
        char buf[4];
        ssize_t nr = read(STDIN_FILENO, buf, sizeof(buf));
        (void)nr;
    }
}

// ─── Activity-tracking sink ───────────────────────────────────────────────────
//
// Wraps LibremidiSink and records the timestamp of the most recent note-on per
// MIDI channel. channel_active() returns true within ACTIVE_NS of any note-on.
// Called from the timing thread (send) and the display thread (channel_active),
// so per-channel state is held in atomics.

class ActivitySink : public OutputSink
{
public:
    static constexpr uint64_t ACTIVE_NS = 150'000'000ULL;  // 150 ms hold time

    explicit ActivitySink(LibremidiSink& inner) noexcept : inner_(inner) {}

    void send(const Event& e) override
    {
        if (e.payload_tag == OMEGA_NOTE_ON && e.channel < 16)
            last_on_[e.channel].store(now_ns(), std::memory_order_relaxed);
        inner_.send(e);
    }

    void flush() override { inner_.flush(); }

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

// ─── Display ──────────────────────────────────────────────────────────────────

static void draw(const Engine& eng, const ActivitySink& activity, bool playing, uint32_t loop_count)
{
    // Convert transport position to musical coordinates.
    uint64_t pos_ns = eng.transport_position_ns();
    double pos_sec = static_cast<double>(pos_ns) / 1.0e9;
    double beats = pos_sec * BPM / 60.0;
    double pat_len = static_cast<double>(BARS * BEATS_PER_BAR);  // 32 beats
    double beat_in = fmod(beats, pat_len);

    auto bar = static_cast<uint32_t>(beat_in / BEATS_PER_BAR) + 1;        // 1-8
    auto beat = static_cast<uint32_t>(fmod(beat_in, BEATS_PER_BAR)) + 1;  // 1-4

    // Elapsed clock time from transport position.
    uint64_t total_s = pos_ns / 1'000'000'000ULL;
    uint64_t mins = total_s / 60;
    uint64_t secs = total_s % 60;
    uint64_t tenths = (pos_ns % 1'000'000'000ULL) / 100'000'000ULL;
    char time_str[16];
    snprintf(time_str,
             sizeof(time_str),
             "%llu:%02llu.%llu",
             static_cast<unsigned long long>(mins),
             static_cast<unsigned long long>(secs),
             static_cast<unsigned long long>(tenths));

    // 32-char progress bar (8 bars × 4 beats each)
    char prog[33];
    prog[32] = '\0';
    auto cur_beat = static_cast<int>((bar - 1) * BEATS_PER_BAR + (beat - 1));
    for (int i = 0; i < 32; ++i)
        prog[i] = (i < cur_beat) ? '#' : (i == cur_beat ? '>' : '.');

    // 8-char bar indicator (one char per bar)
    char bar_ind[9];
    bar_ind[8] = '\0';
    for (uint32_t b = 0; b < BARS; ++b)
        bar_ind[b] = (b < bar - 1) ? '#' : (b == bar - 1 ? '>' : '.');

    // Global MIDI activity
    bool midi_any = activity.any_active();
    bool midi_ok = activity.port_open();

    // Redraw from cursor home
    printf("\033[H");
    printf("=============================================================\n");
    printf("  OMEGA TAPEDECK  v1.0.0    %u BPM  |  4/4  |  8 Bars\n", BPM);
    printf("=============================================================\n");
    printf("  Trk  Name        [1 2 3 4 5 6 7 8]  Ch  MIDI\n");
    printf("  ---  ----------  ----------------   --  ----\n");

    for (uint32_t t = 0; t < NUM_TRACKS; ++t)
    {
        // Track uses MIDI channel t (0-indexed).
        const char* act = activity.channel_active(static_cast<uint8_t>(t)) ? "[*]" : "[ ]";

        if (TRACKS[t].has_content)
            printf("   %u   %-10s  [%s]   %u   %s\n", t + 1, TRACKS[t].name, bar_ind, t + 1, act);
        else
            printf("   %u   %-10s  [--------]   %u   %s\n", t + 1, TRACKS[t].name, t + 1, act);
    }

    printf("-------------------------------------------------------------\n");
    printf("  [%s]\n", prog);
    printf("  Bar %u/8   Beat %u/4   Loop: %u   Time: %s\n", bar, beat, loop_count, time_str);
    printf("  %-14s  MIDI: %s %s\n",
           playing ? "PLAYING >>>" : "STOPPED [=]",
           midi_any ? "[*]" : "[ ]",
           midi_ok ? "connected" : "no port");
    printf("=============================================================\n");
    printf("  SPACE: Play/Stop   ESC: Rewind to Start   Ctrl+C: Exit\n");
    printf("=============================================================\n");
    fflush(stdout);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    // Signal handler for clean Ctrl+C exit.
    struct sigaction sa
    {};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, nullptr);

    setup_terminal();

    // ── Engine & MIDI sinks ──────────────────────────────────────────────────
    Engine engine;

    // LibremidiSink owns the MIDI port; ActivitySink wraps it and is what the
    // engine dispatches to. Pattern events use activity.sink_id().
    LibremidiSink midi_sink(nullptr);  // nullptr = first available MIDI output
    ActivitySink activity(midi_sink);
    engine.add_sink(&activity);
    const uint32_t sid = activity.sink_id();

    // ── Tempo & time signature (direct, before timer) ────────────────────────
    engine.tempo_map().insert(0, BPM_MILLI);
    engine.timesig_map().insert(0, BEATS_PER_BAR, 4);

    // ── Build the 8-bar pattern ──────────────────────────────────────────────
    PatternId pat = engine.create_pattern("8-bar loop", PATTERN_LEN);
    populate_pattern(engine, pat, sid);

    // Queue slot assignment before the timer starts.
    engine.enqueue(PerfAssignCmd{0, pat});

    // ── Start timing thread ──────────────────────────────────────────────────
    OmegaTimer timer(engine, 1000);  // 1 ms interval

    // ── Transport state ───────────────────────────────────────────────────────
    bool playing = false;
    bool slot_cued = false;
    uint32_t loop_count = 0;
    uint64_t last_loop_idx = 0;

    // Clear screen once, then redraw in-place each tick.
    printf("\033[2J");
    draw(engine, activity, playing, loop_count);

    // ── Main loop ────────────────────────────────────────────────────────────
    while (!g_quit.load(std::memory_order_relaxed))
    {
        char ch = read_key();

        if (ch == ' ')
        {
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
        }
        else if (ch == '\x1b')
        {
            // If followed immediately by '[' it's an arrow/function key; swallow it.
            // If nothing follows within 10 ms it's a bare ESC = rewind.
            struct timeval tv = {0, 10000};
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);

            if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) == 0)
            {
                // Bare ESC → rewind to start
                if (playing)
                {
                    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
                    playing = false;
                }
                engine.enqueue(TransportCmd{TransportAction::LOCATE, 0});
                slot_cued = false;
                loop_count = 0;
                last_loop_idx = 0;
            }
            else
            {
                drain_escape();
            }
        }

        // Detect pattern loop wrap.
        if (playing)
        {
            uint64_t pos_ns = engine.transport_position_ns();
            double beats = static_cast<double>(pos_ns) / 1.0e9 * BPM / 60.0;
            auto idx = static_cast<uint64_t>(beats) / (BARS * BEATS_PER_BAR);
            if (idx > last_loop_idx)
            {
                loop_count += static_cast<uint32_t>(idx - last_loop_idx);
                last_loop_idx = idx;
            }
        }

        draw(engine, activity, playing, loop_count);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
    // OmegaTimer destructor joins timing thread and does a final process().

    restore_terminal();
    printf("\n\033[2J\033[H");
    printf("Omega Tapedeck stopped.\n");
    return 0;
}
