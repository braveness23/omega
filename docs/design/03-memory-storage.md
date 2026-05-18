# Design: Memory and Event Storage

## Decision Summary

| Concern | Decision |
|---|---|
| Event storage per track | `std::pmr::vector<Event>` sorted by tick |
| Default allocator | Heap (`std::pmr::new_delete_resource()`) |
| Embedded/pool allocator | `std::pmr::monotonic_buffer_resource` over user-supplied buffer |
| Small payload (≤ 12 bytes) | Stored inline in Event |
| Large payload (sysex, etc.) | Stored in a separate blob store, Event holds an index |
| Recording staging | Per-track unsorted ring buffer, merged on commit |
| Note-off tracking | Separate active-notes table, not stored in event stream |

---

## The Event Type

The core unit. Design target: 24 bytes per event (3 cache lines per 8 events).

```cpp
struct Event {
    uint64_t    tick;        // absolute musical position (8 bytes)
    uint32_t    sink_id;     // which OutputSink receives this event (4 bytes)
    uint8_t     payload_tag; // discriminant for the payload union (1 byte)
    uint8_t     channel;     // MIDI channel 0-15, or 0 for non-MIDI (1 byte)
    uint8_t     reserved[2]; // padding, future use (2 bytes)
    uint8_t     data[8];     // inline payload: 3 MIDI bytes + 4 duration + 1 spare (8 bytes)
};
// sizeof(Event) == 24
```

`payload_tag` values:
- `0x00` — MIDI note on: data[0]=note, data[1]=velocity, data[2-5]=duration_ticks
- `0x01` — MIDI note off: data[0]=note, data[1]=velocity
- `0x02` — MIDI CC: data[0]=controller, data[1]=value
- `0x03` — MIDI program change: data[0]=program
- `0x04` — MIDI pitch bend: data[0-1]=value (14-bit)
- `0x05` — MIDI aftertouch: data[0]=pressure
- `0x06` — MIDI poly aftertouch: data[0]=note, data[1]=pressure
- `0x07` — MIDI sysex: data[0-3]=blob_index (index into blob store)
- `0x08` — Tempo change: data[0-3]=bpm_milli
- `0x09` — OSC message: data[0-3]=blob_index
- `0x0A` — Parameter change: data[0-3]=param_id, data[4-7]=value (float or int)
- `0x0B-0xFE` — Reserved for future types and registered custom types
- `0xFF` — Custom/blob: data[0-3]=type_id, data[4-7]=blob_index

Duration is stored inline in note-on events (data[2-5], 4 bytes = uint32_t ticks). The engine uses this to fire note-off independently of the event stream. Explicit note-off events exist for imported MIDI files where duration is not pre-computed.

---

## Storage Layout: Sorted Vector

Events within a track or pattern are stored in a `std::pmr::vector<Event>` sorted by tick in ascending order.

**Why sorted vector:**
- Sequential playback scans front-to-back: cache-friendly
- Binary search for locate (seek to measure): O(log n)
- Bulk insert during import is O(n log n) sort, done once
- The dominant access pattern (playback) is sequential, not random

**Tradeoff:** Insertion during real-time recording is O(n) in the worst case (insertion into the middle of a sorted array). This is handled by the recording staging buffer — events are never inserted mid-vector during playback.

**Practical sizes:** A 10-minute, 48-track session at moderate density is roughly:
- 48 tracks × ~500 events/track = ~24,000 events
- 24,000 × 24 bytes = 576 KB total
This is small. The sorted vector approach is not a memory problem.

---

## Polymorphic Memory Resources (PMR)

`std::pmr` (C++17) allows swapping the allocator without changing the container type. This is the key to embeddability.

**Default (heap):**
```cpp
auto track_events = std::pmr::vector<Event>{}; // uses new_delete_resource
```

**Embedded / no-heap:**
```cpp
std::byte buffer[1024 * 1024]; // 1MB static buffer
std::pmr::monotonic_buffer_resource pool{buffer, sizeof(buffer)};
auto track_events = std::pmr::vector<Event>{&pool};
```

The `Engine` constructor accepts an optional `std::pmr::memory_resource*`. If null, the default heap allocator is used. This is the only allocation control point — all internal structures use the same resource.

Callers on embedded targets pre-size the buffer based on their known event count (events × 24 bytes + overhead).

---

## The Blob Store

Large payloads (sysex, OSC messages, custom types) do not fit in 8 bytes. They are stored in a separate **blob store**: a flat `std::pmr::vector<uint8_t>` with a free-list for reclamation.

Each blob has a 4-byte header: `uint32_t size`. The `blob_index` in an Event's data field is the byte offset into the blob store.

The blob store is append-only during import/composition. During real-time operation, blobs are reference-counted (a `uint16_t` refcount in the header). Sysex events are rare enough that the overhead of reference counting is acceptable.

For v1, the blob store is a simple append-only vector with no reclamation. Reclamation (free-list or compaction) is deferred to v2.

---

## Recording Staging Buffer

During real-time recording, incoming MIDI events arrive with wall-clock timestamps. They cannot be inserted directly into the sorted track vector without invalidating the playback scan position.

Each recording-enabled track has a **staging ring buffer** — a fixed-size circular buffer of `RecordedEvent` entries:

```cpp
struct RecordedEvent {
    uint64_t ns_timestamp;  // wall-clock time of capture (before tick conversion)
    Event    event;         // pre-filled except for .tick
};
```

Ring buffer size: 1024 entries (configurable). At 120 BPM playing 16th notes across 16 channels, the maximum inflow is ~32 events/second. 1024 entries is ~32 seconds of headroom before overflow. Overflow drops the oldest event and sets a `recording_overflow` flag.

**Commit**: When recording stops (or on explicit commit), the staging buffer is:
1. Drained
2. Timestamps converted to ticks via the tempo map
3. Sorted
4. Merged into the track vector (merge sort, O(n))

During overdub (recording while playing), the staging buffer fills while playback continues. Commit happens at stop or at a loop boundary.

---

## Note-Off Tracking (Active Notes Table)

The engine maintains an **active notes table** per output channel per sink — a flat array tracking currently-playing notes and when their note-off is due.

```cpp
struct ActiveNote {
    uint64_t off_tick;   // when to fire note-off
    uint8_t  note;       // MIDI note number
    uint8_t  velocity;   // note-off velocity (usually 0 or 64)
    uint8_t  channel;
    uint8_t  sink_id;
};
```

Max simultaneous notes: 128 per sink/channel (one per MIDI note number). This is a fixed-size array of 128 entries, O(1) lookup and update by note number.

On each engine cycle, after firing events, the engine scans the active notes table for any entries where `off_tick <= current_tick` and fires note-off messages. This is borrowed from KCS's design — note-offs are tracked separately from the event stream.

This also serves as **stuck-note prevention** (borrowing from jdksmidi's note matrix): on transport stop, all active notes receive note-off immediately.

---

## Pattern Storage

A `Pattern` is:
```cpp
struct Pattern {
    std::string             name;
    uint64_t                length_ticks;    // loop length; 0 = one-shot
    std::pmr::vector<Event> events;
    uint32_t                id;
};
```

Patterns are owned by the `PatternLibrary`, a flat `std::pmr::vector<Pattern>` indexed by `PatternId` (uint32_t). Pattern IDs are stable after creation (never reused within a session).

---

## Open Issues

- **Blob store reclamation**: The append-only blob store leaks memory when sysex events are deleted. Track the issue; implement free-list in v2.
- **Very large events**: Maximum sysex size? Cap at 64KB for v1 (fits in a `uint16_t` size field in the blob header).
- **Thread safety of staging buffer**: The staging buffer is written by the MIDI input callback and read by the timing thread at commit time. Use the command queue for commit triggering, and make the staging buffer SPSC.
