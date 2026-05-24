#include <omega/anchor_point.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace omega
{

void AnchorList::add(std::string name, uint64_t offset_ticks, uint32_t flags)
{
    auto it = std::lower_bound(
        anchors_.begin(), anchors_.end(), offset_ticks, [](const AnchorPoint& a, uint64_t t) {
            return a.offset_ticks < t;
        });
    auto inserted_pos = static_cast<int32_t>(it - anchors_.begin());
    anchors_.insert(it, {std::move(name), offset_ticks, flags});
    // Shift active_snap_index_ if it falls at or after the insertion point.
    if (active_snap_index_ >= inserted_pos)
    {
        ++active_snap_index_;
    }
}

omega_status_t AnchorList::remove(const std::string& name)
{
    for (auto it = anchors_.begin(); it != anchors_.end(); ++it)
    {
        if (it->name == name)
        {
            auto removed_pos = static_cast<int32_t>(it - anchors_.begin());
            anchors_.erase(it);
            if (active_snap_index_ == removed_pos)
            {
                active_snap_index_ = -1;
            }
            else if (active_snap_index_ > removed_pos)
            {
                --active_snap_index_;
            }
            return OMEGA_OK;
        }
    }
    return OMEGA_ERR_NOT_FOUND;
}

const AnchorPoint* AnchorList::at(uint32_t index) const noexcept
{
    if (index >= static_cast<uint32_t>(anchors_.size()))
    {
        return nullptr;
    }
    return &anchors_[index];
}

const AnchorPoint* AnchorList::find_by_name(const std::string& name) const noexcept
{
    for (const auto& a : anchors_)
    {
        if (a.name == name)
        {
            return &a;
        }
    }
    return nullptr;
}

std::vector<const AnchorPoint*> AnchorList::snap_anchors() const
{
    std::vector<const AnchorPoint*> result;
    for (const auto& a : anchors_)
    {
        if ((a.flags & ANCHOR_SNAP) != 0u)
        {
            result.push_back(&a);
        }
    }
    return result;
}

omega_status_t AnchorList::set_active_snap(uint32_t index)
{
    if (index >= static_cast<uint32_t>(anchors_.size()))
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    if ((anchors_[index].flags & ANCHOR_SNAP) == 0u)
    {
        return OMEGA_ERR_INVALID;
    }
    active_snap_index_ = static_cast<int32_t>(index);
    return OMEGA_OK;
}

const AnchorPoint* AnchorList::active_snap() const noexcept
{
    if (active_snap_index_ < 0 || active_snap_index_ >= static_cast<int32_t>(anchors_.size()))
    {
        return nullptr;
    }
    return &anchors_[static_cast<uint32_t>(active_snap_index_)];
}

void AnchorList::clear() noexcept
{
    anchors_.clear();
    active_snap_index_ = -1;
}

}  // namespace omega
