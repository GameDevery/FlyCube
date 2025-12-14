#pragma once
#include "View/View.h"

class BindlessTypedViewPool;
class Device;

class ViewBase : public View {
public:
    ViewBase(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc);

    std::shared_ptr<Resource> GetResource() override;
    uint32_t GetDescriptorId() const override;
    uint32_t GetBaseMipLevel() const override;
    uint32_t GetLevelCount() const override;
    uint32_t GetBaseArrayLayer() const override;
    uint32_t GetLayerCount() const override;

protected:
    void CreateBindlessTypedViewPool(Device& device);

    std::shared_ptr<Resource> resource_;
    ViewDesc view_desc_;
    std::shared_ptr<BindlessTypedViewPool> bindless_view_pool_;
};
