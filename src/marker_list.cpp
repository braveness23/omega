#include <omega/marker_list.h>

#include <algorithm>
#include <utility>

namespace omega
{

void MarkerList::add(std::string name, uint64_t tick)
{
    auto it =
        std::lower_bound(markers_.begin(), markers_.end(), tick, [](const Marker& m, uint64_t t) {
            return m.tick < t;
        });
    markers_.insert(it, {std::move(name), tick});
}

omega_status_t MarkerList::remove(uint32_t index)
{
    if (index >= static_cast<uint32_t>(markers_.size()))
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    markers_.erase(markers_.begin() + static_cast<ptrdiff_t>(index));
    return OMEGA_OK;
}

const Marker* MarkerList::at(uint32_t index) const noexcept
{
    if (index >= static_cast<uint32_t>(markers_.size()))
    {
        return nullptr;
    }
    return &markers_[index];
}

const Marker* MarkerList::find_nearest(uint64_t tick) const noexcept
{
    if (markers_.empty())
    {
        return nullptr;
    }
    auto it =
        std::upper_bound(markers_.begin(), markers_.end(), tick, [](uint64_t t, const Marker& m) {
            return t < m.tick;
        });
    if (it == markers_.begin())
    {
        return nullptr;
    }
    --it;
    return &*it;
}

void MarkerList::clear() noexcept
{
    markers_.clear();
}

}  // namespace omega
