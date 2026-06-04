#pragma once

#include <cstdint>
#include <string>

namespace omega
{

/*
 * A Standard MIDI File text-class meta event, preserved verbatim so that an
 * imported SMF round-trips back out without losing its descriptive metadata.
 *
 * `type` is the raw SMF meta-event type byte:
 *   0x01 Text        0x02 Copyright Notice
 *   0x04 Instrument  0x05 Lyric
 *
 * Two other SMF text-class events have dedicated homes and are NOT stored here:
 *   0x03 Track Name  → Track::name
 *   0x06 Marker / 0x07 Cue Point → MarkerList
 *
 * `tick` is in omega ticks (480 PPQN), scaled from the SMF's own resolution on
 * import — positional events such as lyrics keep their place in the timeline.
 * Events that belong to a specific timeline track live in Track::meta; events
 * from a note-less conductor track (typically a file-level copyright) live in
 * Engine::session_meta().
 */
struct MetaEvent
{
    uint64_t tick{0};
    uint8_t type{0};
    std::string text;
};

}  // namespace omega
