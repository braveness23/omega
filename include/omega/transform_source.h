#pragma once

#include <omega/event_source.h>

#include <array>
#include <cstdint>

namespace omega
{

/*
 * Abstract base for composition-based routing.
 *
 * Wraps an upstream EventSource and intercepts all events it would dispatch.
 * Subclasses override transform() to modify or drop individual events before
 * they reach the downstream EventDispatcher (and ultimately the OutputSinks).
 *
 * Usage pattern:
 *   class MyFilter : public TransformSource {
 *   protected:
 *       bool transform(Event& e) override { return e.channel != 9; }  // drop ch 9
 *   public:
 *       explicit MyFilter(EventSource& upstream) : TransformSource(upstream) {}
 *   };
 *
 * Thread: advance() is called from the timing thread. Must never allocate
 * (the internal capture buffer is stack-allocated with a fixed capacity).
 */
class TransformSource : public EventSource
{
public:
    ~TransformSource() override = default;

    TransformSource(const TransformSource&) = delete;
    TransformSource& operator=(const TransformSource&) = delete;
    TransformSource(TransformSource&&) = delete;
    TransformSource& operator=(TransformSource&&) = delete;

    void advance(uint64_t to_tick, EventDispatcher& downstream, ProcessContext& ctx) final;

    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;

protected:
    explicit TransformSource(EventSource& upstream) noexcept : upstream_{&upstream} {}

    /*
     * Called for each event emitted by the upstream source.
     * Modify `e` in place to transform it; return false to drop it.
     * Thread: Timing thread only. Must never allocate, block, or lock.
     */
    virtual bool transform(Event& event) = 0;

private:
    /*
     * Captures events from the upstream into a fixed-size stack buffer.
     * Events beyond CAPACITY are silently dropped.
     */
    struct CaptureDispatcher : EventDispatcher
    {
        static constexpr uint32_t CAPACITY = 512;

        CaptureDispatcher() noexcept = default;

        void dispatch(const Event& event) noexcept override
        {
            if (count < CAPACITY)
            {
                events[count++] = event;
            }
        }

        std::array<Event, CAPACITY> events{};
        uint32_t count{0};
    };

    EventSource* upstream_;
};

/*
 * Concrete TransformSource: drops all events not on a specified MIDI channel.
 *
 * Example:
 *   ChannelFilterSource filter(timeline_source, 0);  // keep only channel 0
 */
class ChannelFilterSource : public TransformSource
{
public:
    explicit ChannelFilterSource(EventSource& upstream, uint8_t channel) noexcept
        : TransformSource(upstream), channel_{channel}
    {}

protected:
    bool transform(Event& event) override { return event.channel == channel_; }

private:
    uint8_t channel_;
};

}  // namespace omega
