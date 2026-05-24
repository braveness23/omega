#include <omega/event_anchor_table.h>

namespace omega
{

AnchorList& EventAnchorTable::get_or_create(uint32_t container_id, uint32_t event_index)
{
    return table_[key(container_id, event_index)];
}

AnchorList* EventAnchorTable::get(uint32_t container_id, uint32_t event_index) noexcept
{
    auto it = table_.find(key(container_id, event_index));
    if (it == table_.end())
    {
        return nullptr;
    }
    return &it->second;
}

const AnchorList* EventAnchorTable::get(uint32_t container_id, uint32_t event_index) const noexcept
{
    auto it = table_.find(key(container_id, event_index));
    if (it == table_.end())
    {
        return nullptr;
    }
    return &it->second;
}

omega_status_t EventAnchorTable::remove(uint32_t container_id, uint32_t event_index)
{
    auto it = table_.find(key(container_id, event_index));
    if (it == table_.end())
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    table_.erase(it);
    return OMEGA_OK;
}

void EventAnchorTable::clear() noexcept
{
    table_.clear();
}

}  // namespace omega
