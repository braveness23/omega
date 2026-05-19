# Design: Thread Model

## Decision

**Caller-driven engine with a lock-free mutation queue.**

The engine has no threads of its own. The caller is responsible for calling `engine.process()` from a timing thread at whatever interval suits their application. Mutations from other threads (the UI, the API) are delivered through a lock-free SPSC queue that the engine drains at the start of each `process()` call.

Omega optionally provides `OmegaTimer` — a RAII wrapper that runs `process()` on a dedicated high-priority thread for applications that don't have their own timing source.

---

## Why Caller-Driven

A sequencer library has no idea what threading context it will be called from:

- An audio plugin is called from the DAW's audio thread. The library must not create its own threads.
- A hardware device may drive the sequencer from a hardware timer ISR.
- A test harness drives the sequencer with a `MockClock` and no real time at all.
- A game engine calls the sequencer from its update loop.

If the library owned its thread, all of these scenarios would require fighting the library. Caller-driven means the library works everywhere.

---

## The Two Contexts

There are exactly two thread contexts in Omega:

**The timing thread** calls `engine.process()`. This is the hot path. It must never block, never allocate, never lock. It drains the command queue, then calls `advance()` on every registered `EventSource` in registration order, dispatching all due events to registered `OutputSink` instances.

**The mutation thread** (typically the UI or API thread) adds events, changes tempo, edits patterns, registers sources. It enqueues commands and returns immediately.

These two threads communicate through a **lock-free SPSC queue** (single producer, single consumer). The timing thread is always the consumer. The mutation thread is always the producer.

---

## The Command Queue

Mutations are encoded as `Command` objects and enqueued. The engine drains the queue at the top of each `process()` call, before doing any playback work.

```cpp
// Command variants — all are small, value types
struct AddEventCmd   { TrackId track; Event event; };
struct DeleteEventCmd { TrackId track; EventId event; };
struct SetTempoCmd   { uint32_t bpm_milli; };
struct SetLoopCmd    { uint64_t start_tick; uint64_t end_tick; bool enabled; };
struct CuePatternCmd { SlotId slot; PatternId pattern; CueMode mode; };
struct TransportCmd  { TransportAction action; uint64_t locate_tick; };
// ... etc.

using Command = std::variant<
    AddEventCmd, DeleteEventCmd, SetTempoCmd,
    SetLoopCmd, CuePatternCmd, TransportCmd
    // ...
>;
```

The SPSC queue is a power-of-two ring buffer of `Command` objects, allocated at engine construction. Default capacity: 4096 commands. Configurable at construction time.

If the queue is full (mutation thread is producing faster than the timing thread consumes), the enqueue operation returns `OMEGA_ERR_QUEUE_FULL`. The caller decides how to handle this — retry, discard, or grow a staging buffer. The timing thread never stalls.

---

## SPSC Queue Implementation

Omega ships its own SPSC ring buffer. It is wait-free for both producer and consumer. The implementation uses two `std::atomic<uint32_t>` indices (head and tail) with relaxed/release/acquire ordering.

```cpp
template<typename T, uint32_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    T storage_[Capacity];
public:
    bool push(T&& item);   // producer: returns false if full
    bool pop(T& out);      // consumer: returns false if empty
};
```

Cache-line alignment of `head_` and `tail_` prevents false sharing between producer and consumer cores.

**Why not use a third-party queue?** The queue is simple enough to own. External dependencies for something this small create more maintenance burden than they solve. If a known-good external option is later preferred (moodycamel's `ReaderWriterQueue`, BSD licensed), the interface is small enough to swap.

---

## Memory in the Hot Path

The timing thread must not allocate. Rules for `engine.process()`:

- No `new`, no `malloc`, no `std::vector::push_back` that may resize
- No `std::mutex::lock`
- No system calls
- No I/O (logging goes through a lock-free log ring buffer, drained off the timing thread)

Event dispatch (calling `OutputSink::send()`) may interact with hardware (MIDI port write). This is unavoidable — MIDI output is inherently I/O. The OS MIDI API should be non-blocking for normal writes; if it blocks, the timing thread blocks, and timing degrades. This is a known limitation of software MIDI and is not unique to Omega.

---

## The Optional Timer Thread (`OmegaTimer`)

For applications that don't have a timing thread:

```cpp
class OmegaTimer {
public:
    // Starts a high-priority thread that calls engine.process() at interval_us
    OmegaTimer(Engine& engine, uint32_t interval_us = 1000);
    ~OmegaTimer();  // joins the thread
};
```

The thread uses platform-specific high-resolution sleep (`nanosleep` on Linux, `timeBeginPeriod`/`Sleep` on Windows, `mach_wait_until` on macOS). It is not a real-time thread — it does not use `SCHED_FIFO` or equivalent by default, but the caller can set thread priority after construction.

`OmegaTimer` is a convenience. It is not the recommended path for audio plugins or hardware devices.

---

## Callbacks and Thread Safety

`OutputSink::send()` is called from the timing thread. If a sink implementation touches UI state, it must marshal that to the UI thread itself — Omega will not do it. The sink is responsible for its own thread safety.

The recording callback (`on_event_captured`) is also called from the timing thread (MIDI input is processed during `engine.process()`). Same rules apply.

---

## Undo/Redo

Undo/redo is implemented via the Command pattern (borrowed from TSE3's design). Every mutation is a `Command` with both `execute()` and `undo()` semantics. The mutation thread maintains a `CommandHistory` stack. `undo()` posts the inverse command to the queue.

The timing thread never needs to know about undo history. It only sees commands.

---

## Diagrams

```
Mutation Thread          Timing Thread
─────────────            ──────────────
engine.set_tempo(120)
  → enqueue SetTempoCmd
  → return immediately   process():
                           drain command queue
                             apply SetTempoCmd
                           for each EventSource:
                             source.advance(to_tick, dispatcher)
                               → dispatcher.dispatch(event)
                                   → sink.send(event)
```

```
Thread safety matrix:
                        Timing Thread    Mutation Thread
engine.process()            ✓                ✗
engine.enqueue_cmd()        ✗*               ✓
source.advance()            ✓ (called by)    ✗
sink.send()                 ✓ (called by)    ✗
clock.now_ns()              ✓                ✗

* enqueue_cmd() is safe from any thread as long as only one thread
  is the producer at a time (SPSC invariant).
```

---

## Open Issues

- **Multiple mutation threads**: The SPSC queue is single-producer. If multiple threads need to mutate (e.g., a UI thread and a network sync thread), they must serialize through a mutex on the producer side, or use a MPSC queue. Defer to v2 — document the single-producer requirement clearly.
- **Real-time priority**: `OmegaTimer` does not automatically acquire real-time scheduling. Whether this matters depends on the host OS and workload. Document, don't decide.
- **Logging from the timing thread**: Design the lock-free log ring buffer before implementing. Don't add fprintf to the hot path.
- **Source registration from the timing thread**: `EventSourceRegistry::add()` and `remove()` must go through the command queue to avoid data races. The registry is read exclusively by the timing thread during `advance_all()`; writes from the mutation thread must be deferred.
