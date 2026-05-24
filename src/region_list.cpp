#include <omega/region_list.h>

#include <algorithm>
#include <utility>

namespace omega
{

omega_status_t RegionList::add(std::string name, uint64_t start, uint64_t end, RegionType type)
{
    if (start >= end)
    {
        return OMEGA_ERR_INVALID;
    }
    auto it =
        std::lower_bound(regions_.begin(), regions_.end(), start, [](const Region& r, uint64_t t) {
            return r.start_tick < t;
        });
    regions_.insert(it, {std::move(name), start, end, type});
    return OMEGA_OK;
}

omega_status_t RegionList::remove(uint32_t index)
{
    if (index >= static_cast<uint32_t>(regions_.size()))
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    regions_.erase(regions_.begin() + static_cast<ptrdiff_t>(index));
    return OMEGA_OK;
}

const Region* RegionList::at(uint32_t index) const noexcept
{
    if (index >= static_cast<uint32_t>(regions_.size()))
    {
        return nullptr;
    }
    return &regions_[index];
}

const Region* RegionList::find_containing(uint64_t tick) const noexcept
{
    for (const auto& r : regions_)
    {
        if (r.start_tick <= tick && tick < r.end_tick)
        {
            return &r;
        }
    }
    return nullptr;
}

void RegionList::clear() noexcept
{
    regions_.clear();
}

}  // namespace omega
