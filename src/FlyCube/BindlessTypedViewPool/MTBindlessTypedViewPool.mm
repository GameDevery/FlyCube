#include "BindlessTypedViewPool/MTBindlessTypedViewPool.h"

#include "Device/MTDevice.h"
#include "Utilities/Cast.h"
#include "Utilities/Check.h"
#include "View/MTView.h"

#if defined(USE_METAL_SHADER_CONVERTER)
#include <metal_irconverter_runtime.h>
using EntryType = IRDescriptorTableEntry;
#else
using EntryType = uint64_t;
#endif

MTBindlessTypedViewPool::MTBindlessTypedViewPool(MTDevice& device, ViewType view_type, uint32_t view_count)
    : view_count_(view_count)
{
    decltype(auto) argument_buffer = device.GetBindlessArgumentBuffer();
    range_ = std::make_shared<MTGPUArgumentBufferRange>(argument_buffer.Allocate(view_count));
}

uint32_t MTBindlessTypedViewPool::GetBaseDescriptorId() const
{
    return range_->GetOffset();
}

uint32_t MTBindlessTypedViewPool::GetViewCount() const
{
    return view_count_;
}

void MTBindlessTypedViewPool::WriteView(uint32_t index, const std::shared_ptr<View>& view)
{
    WriteViewImpl(index, view.get());
}

void MTBindlessTypedViewPool::WriteViewImpl(uint32_t index, View* view)
{
    DCHECK(index < view_count_);
    auto* mt_view = CastToImpl<MTView>(view);
    EntryType* arguments = static_cast<EntryType*>(range_->GetArgumentBuffer().contents);
    const uint32_t offset = range_->GetOffset() + index;
#if defined(USE_METAL_SHADER_CONVERTER)
    mt_view->BindView(&arguments[offset]);
#else
    arguments[offset] = mt_view->GetGpuAddress();
#endif
    range_->AddAllocation(offset, mt_view->GetAllocation());
}
