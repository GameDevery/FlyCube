#pragma once
#include "Resource/MTResource.h"
#include "View/ViewBase.h"

class MTDevice;
class MTResource;

class MTView : public ViewBase {
public:
    MTView(MTDevice& device, const std::shared_ptr<MTResource>& resource, const ViewDesc& view_desc);

    id<MTLTexture> GetTextureView() const;
    id<MTLResource> GetAllocation() const;
    MTLGPUAddress GetGpuAddress() const;
    void BindView(id<MTL4ArgumentTable> argument_table, uint32_t index);

private:
    void CreateView();
    void CreateTextureView();
    void CreateTextureBufferView();

    MTDevice& device_;
    MTResource* mt_resource_;
    id<MTLTexture> texture_view_ = nullptr;
};
