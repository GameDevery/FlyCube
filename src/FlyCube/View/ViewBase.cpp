#include "View/ViewBase.h"

#include "BindlessTypedViewPool/BindlessTypedViewPoolBase.h"
#include "Device/Device.h"
#include "Resource/Resource.h"
#include "Utilities/Cast.h"
#include "Utilities/NotReached.h"

ViewBase::ViewBase(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc)
    : resource_(resource)
    , view_desc_(view_desc)
{
}

void ViewBase::CreateBindlessTypedViewPool(Device& device)
{
    bindless_view_pool_ = device.CreateBindlessTypedViewPool(view_desc_.view_type, 1);
    auto* base_bindless_view_pool_ = CastToImpl<BindlessTypedViewPoolBase>(bindless_view_pool_);
    base_bindless_view_pool_->WriteViewImpl(0, this);
}

std::shared_ptr<Resource> ViewBase::GetResource()
{
    return resource_;
}

uint32_t ViewBase::GetDescriptorId() const
{
    if (bindless_view_pool_) {
        return bindless_view_pool_->GetBaseDescriptorId();
    }
    NOTREACHED();
}

uint32_t ViewBase::GetBaseMipLevel() const
{
    return view_desc_.base_mip_level;
}

uint32_t ViewBase::GetLevelCount() const
{
    return std::min<uint32_t>(view_desc_.level_count, resource_->GetLevelCount() - view_desc_.base_mip_level);
}

uint32_t ViewBase::GetBaseArrayLayer() const
{
    return view_desc_.base_array_layer;
}

uint32_t ViewBase::GetLayerCount() const
{
    return std::min<uint32_t>(view_desc_.layer_count, resource_->GetLayerCount() - view_desc_.base_array_layer);
}
