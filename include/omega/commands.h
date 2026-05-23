#pragma once

#include <omega/types.h>

#include <cstdint>
#include <variant>

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
                             PerfSetRandomBiasCmd>;

static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");

}  // namespace omega
