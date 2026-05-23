#pragma once

#include <omega/omega.h>
#include <omega/smpte_converter.h>
#include <omega/types.h>

#include <cstdint>
#include <variant>

namespace omega
{
class EventInput;   // forward declaration for AddInputCmd / RemoveInputCmd
class EventSource;  // forward declaration for AddSourceCmd / RemoveSourceCmd
}  // namespace omega

namespace omega
{

struct AddEventCmd
{
    TrackId track;
    Event event;
};

struct DeleteEventCmd
{
    TrackId track;
    uint64_t tick;
    uint32_t index;
};

struct SetTempoCmd
{
    uint32_t bpm_milli;
};

struct SetLoopCmd
{
    uint64_t start_tick;
    uint64_t end_tick;
    bool enabled;
};

struct CuePatternCmd
{
    SlotId slot;
    PatternId pattern;
    CueMode mode;
};

struct TransportCmd
{
    TransportAction action;
    uint64_t locate_tick;
};

struct SongAppendCmd
{
    PatternId pattern_id;
    uint32_t repeat_count;
};

struct SongClearCmd
{};

struct PerfAssignCmd
{
    SlotId slot;
    PatternId pattern;
};

struct PerfCueCmd
{
    SlotId slot;
    CueMode mode;
};

struct PerfStopCmd
{
    SlotId slot;
    CueMode mode;
};

struct PerfStopAllCmd
{
    CueMode mode;
};

struct PerfSetTransposeCmd
{
    SlotId slot;
    int8_t semitones;
};

struct PerfSetVelocityScaleCmd
{
    SlotId slot;
    uint8_t scale;
};

struct PerfSetRandomBiasCmd
{
    SlotId slot;
    uint8_t bias;
};

struct AddInputCmd
{
    EventInput* input;  // non-owning
};

struct RemoveInputCmd
{
    EventInput* input;  // non-owning
};

// ── M4.3 performance context commands ────────────────────────────────────────

struct SetCtxScaleCmd
{
    omega_scale_t scale;
};

struct SetCtxChordCmd
{
    omega_chord_t chord;
};

struct SetCtxTransposeCmd
{
    int8_t semitones;
};

struct SetCtxVelocityCmd
{
    uint8_t velocity;
};

struct SetCtxChaosCmd
{
    uint8_t chaos;
};

struct SetCtxGrooveCmd
{
    uint8_t groove_id;
    float swing;
};

// ── M4.5 time signature commands ─────────────────────────────────────────────

struct SetTimeSigCmd
{
    uint64_t tick;
    uint8_t numerator;
    uint8_t denominator;
};

struct RemoveTimeSigCmd
{
    uint64_t tick;
};

struct ClearTimeSigCmd
{};

// ── M4.5 SMPTE config commands ────────────────────────────────────────────────

struct SetSmpteConfigCmd
{
    SmpteConfig config;
};

struct ClearSmpteConfigCmd
{};

// ── M4.4 custom source commands ───────────────────────────────────────────────

struct AddSourceCmd
{
    EventSource* source;  // non-owning
    uint32_t priority;
};

struct RemoveSourceCmd
{
    EventSource* source;  // non-owning
};

using Command = std::variant<AddEventCmd,
                             DeleteEventCmd,
                             SetTempoCmd,
                             SetLoopCmd,
                             CuePatternCmd,
                             TransportCmd,
                             SongAppendCmd,
                             SongClearCmd,
                             PerfAssignCmd,
                             PerfCueCmd,
                             PerfStopCmd,
                             PerfStopAllCmd,
                             PerfSetTransposeCmd,
                             PerfSetVelocityScaleCmd,
                             PerfSetRandomBiasCmd,
                             AddInputCmd,
                             RemoveInputCmd,
                             SetCtxScaleCmd,
                             SetCtxChordCmd,
                             SetCtxTransposeCmd,
                             SetCtxVelocityCmd,
                             SetCtxChaosCmd,
                             SetCtxGrooveCmd,
                             AddSourceCmd,
                             RemoveSourceCmd,
                             SetTimeSigCmd,
                             RemoveTimeSigCmd,
                             ClearTimeSigCmd,
                             SetSmpteConfigCmd,
                             ClearSmpteConfigCmd>;

static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");

}  // namespace omega
