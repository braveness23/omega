# Key Class Relationships

This diagram shows the primary relationships between Omega's public and internal classes.
Implementation details (private members, helper classes) are omitted for clarity.

```mermaid
classDiagram
    class Engine {
        +enqueue(Command) omega_status_t
        +process() void
        +transport_state() TransportState
        +transport_position_ns() uint64_t
    }

    class EventSource {
        <<abstract>>
        +advance(to_tick, dispatcher, ctx) void
        +on_locate(tick, dispatcher, ctx) void
    }

    class OutputSink {
        <<abstract>>
        +send(Event) void
        +flush() void
        +sink_id() uint32_t
    }

    class ClockSource {
        <<abstract>>
        +now_ns() uint64_t
    }

    class EventInput {
        <<abstract>>
        +poll(dispatcher) void
    }

    class TimelineSource {
        +add_track(name) TrackId
        +add_event(track, event) status
    }

    class SongArrangementSource {
        +append(pattern_id, repeats) void
        +clear() void
    }

    class PerformanceSource {
        +assign(slot, pattern) void
        +cue(slot, mode, tick) void
        +stop(slot, mode, tick) void
    }

    class TransformSource {
        <<abstract>>
        +transform(Event) bool
    }

    class LibremidiSink
    class CapturingSink
    class InternalClock
    class MockClock
    class LibremidiInput
    class MockEventInput

    Engine "1" *-- "1" TimelineSource : owns
    Engine "1" *-- "1" SongArrangementSource : owns
    Engine "1" *-- "1" PerformanceSource : owns
    Engine "1" o-- "0..*" OutputSink : registry
    Engine "1" o-- "1" ClockSource : non-owning ref
    Engine "1" o-- "0..*" EventInput : registry
    Engine "1" o-- "0..*" EventSource : custom sources

    EventSource <|-- TimelineSource
    EventSource <|-- SongArrangementSource
    EventSource <|-- PerformanceSource
    EventSource <|-- TransformSource
    TransformSource o-- EventSource : wraps

    OutputSink <|-- LibremidiSink
    OutputSink <|-- CapturingSink

    ClockSource <|-- InternalClock
    ClockSource <|-- MockClock

    EventInput <|-- LibremidiInput
    EventInput <|-- MockEventInput
```

## Ownership Rules

- `Engine` **owns** the three built-in sources (`TimelineSource`, `SongArrangementSource`,
  `PerformanceSource`) — they are created and destroyed with the engine.
- `Engine` holds **non-owning references** to sinks, clocks, custom sources, and inputs.
  Callers own these objects and must ensure they outlive the engine.
- Every `omega_*_create()` in the C API returns a caller-owned handle; the caller must
  call the matching `omega_*_destroy()` before `omega_engine_destroy()`.
