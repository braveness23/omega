#include <omega/transform_source.h>

namespace omega
{

void TransformSource::advance(uint64_t to_tick, EventDispatcher& downstream, ProcessContext& ctx)
{
    CaptureDispatcher cap;
    upstream_->advance(to_tick, cap, ctx);
    for (uint32_t i = 0; i < cap.count; ++i)
    {
        Event e = cap.events[i];
        if (transform(e))
        {
            downstream.dispatch(e);
        }
    }
}

void TransformSource::on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx)
{
    upstream_->on_locate(tick, chase_out, ctx);
}

}  // namespace omega
