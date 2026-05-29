#pragma once

#include <omega/omega.h>

#include <cstdint>

namespace omega
{

/* Ticks per quarter note — C++ constexpr mirror of OMEGA_PPQN. */
constexpr uint32_t PPQN = OMEGA_PPQN;

/* Event: same memory layout as omega_event_t in the C API. */
using Event = ::omega_event_t;

static_assert(sizeof(Event) == 24, "Event must be exactly 24 bytes");

/* payload_tag constants as a scoped enum for C++ code. */
enum class PayloadTag : uint8_t
{
    NOTE_ON = OMEGA_NOTE_ON,
    NOTE_OFF = OMEGA_NOTE_OFF,
    CC = OMEGA_CC,
    PROGRAM = OMEGA_PROGRAM,
    PITCH_BEND = 0x04,
    AFTERTOUCH = 0x05,
    POLY_AT = 0x06,
    SYSEX = 0x07,
    TEMPO = 0x08,
    OSC = 0x09,
    PARAM = 0x0A,
    CTRL_START_SLOT = OMEGA_CTRL_START_SLOT,
    CTRL_STOP_SLOT = OMEGA_CTRL_STOP_SLOT,
    CTRL_SET_TEMPO = OMEGA_CTRL_SET_TEMPO,
    CTRL_TRANSPOSE = OMEGA_CTRL_TRANSPOSE,
    CUSTOM = 0xFF,
};

/* Opaque ID types used throughout the engine and C++ API. */
using TrackId = uint32_t;
using SlotId = uint32_t;
using PatternId = uint32_t;

/* Cue timing mode for performance-slot operations. */
enum class CueMode : uint8_t
{
    IMMEDIATE,
    NEXT_BEAT,
    NEXT_BAR,
};

/* Transport control actions sent via TransportCmd. */
enum class TransportAction : uint8_t
{
    PLAY,
    STOP,
    LOCATE,
};

/* Observable transport state, readable from any thread. */
enum class TransportState : uint8_t
{
    STOPPED,
    PLAYING,
};

}  // namespace omega
