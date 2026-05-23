#include <omega/pattern_library.h>

#include <algorithm>

namespace omega
{

PatternId PatternLibrary::create(std::string name, uint64_t length_ticks)
{
    PatternId id = next_id_++;
    patterns_.emplace(id, std::make_unique<Pattern>(id, std::move(name), length_ticks));
    return id;
}

Pattern* PatternLibrary::get(PatternId id) noexcept
{
    auto it = patterns_.find(id);
    if (it == patterns_.end())
    {
        return nullptr;
    }
    return it->second.get();
}

const Pattern* PatternLibrary::get(PatternId id) const noexcept
{
    auto it = patterns_.find(id);
    if (it == patterns_.end())
    {
        return nullptr;
    }
    return it->second.get();
}

void PatternLibrary::destroy(PatternId id)
{
    patterns_.erase(id);
}

omega_status_t PatternLibrary::add_event(PatternId id, Event event)
{
    Pattern* pat = get(id);
    if (pat == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    auto pos = std::lower_bound(
        pat->events.begin(), pat->events.end(), event.tick, [](const Event& e, uint64_t tick) {
            return e.tick < tick;
        });
    pat->events.insert(pos, event);
    return OMEGA_OK;
}

omega_status_t PatternLibrary::set_length(PatternId id, uint64_t length_ticks)
{
    Pattern* pat = get(id);
    if (pat == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    pat->length_ticks = length_ticks;
    return OMEGA_OK;
}

}  // namespace omega
