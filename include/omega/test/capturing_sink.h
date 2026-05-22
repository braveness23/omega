#pragma once

#include <omega/sink.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace omega
{

/*
 * Test sink that records every send() call in arrival order.
 *
 * Not suitable for use from a real timing thread — the underlying vector may
 * allocate. Use in single-threaded unit tests only.
 *
 * Thread: not thread-safe.
 */
class CapturingSink final : public OutputSink
{
public:
    /*
     * Thread: Timing thread only (in tests this is the same thread as the test).
     */
    void send(const Event& event) override { events_.push_back(event); }

    /*
     * Thread: Timing thread only.
     */
    void flush() override {}

    /*
     * Returns the number of events captured so far.
     * Thread: Any thread (in tests, called after process() returns).
     */
    [[nodiscard]] size_t count() const noexcept { return events_.size(); }

    /*
     * Returns true if a NOTE_ON event with the given note and channel exists.
     * Thread: Any thread.
     */
    [[nodiscard]] bool has_note_on(uint8_t note, uint8_t channel = 0) const noexcept
    {
        return std::any_of(events_.begin(), events_.end(), [note, channel](const Event& ev) {
            return ev.payload_tag == OMEGA_NOTE_ON && ev.data[0] == note && ev.channel == channel;
        });
    }

    /*
     * Returns true if a NOTE_OFF event with the given note and channel exists.
     * Thread: Any thread.
     */
    [[nodiscard]] bool has_note_off(uint8_t note, uint8_t channel = 0) const noexcept
    {
        return std::any_of(events_.begin(), events_.end(), [note, channel](const Event& ev) {
            return ev.payload_tag == OMEGA_NOTE_OFF && ev.data[0] == note && ev.channel == channel;
        });
    }

    /*
     * Returns the first captured event. Undefined behaviour if count() == 0.
     * Thread: Any thread.
     */
    [[nodiscard]] const Event& first() const { return events_.front(); }

    /*
     * Returns the event at the given index. Undefined behaviour if index >= count().
     * Thread: Any thread.
     */
    [[nodiscard]] const Event& at(size_t index) const { return events_[index]; }

    /*
     * Discards all captured events.
     * Thread: Any thread.
     */
    void clear() noexcept { events_.clear(); }

private:
    std::vector<Event> events_;
};

}  // namespace omega
