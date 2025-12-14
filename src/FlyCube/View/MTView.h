#pragma once
#include "Resource/MTResource.h"
#include "View/ViewBase.h"

class MTDevice;
class MTResource;

class MTView : public ViewBase {
public:
    MTView(MTDevice& device, const std::shared_ptr<MTResource>& resource, const ViewDesc& view_desc);

    const ViewDesc& GetViewDesc() const;
    id<MTLResource> GetNativeResource() const;
    uint64_t GetGpuAddress() const;
    id<MTLBuffer> GetBuffer() const;
    id<MTLSamplerState> GetSampler() const;
    id<MTLTexture> GetTexture() const;
    id<MTLAccelerationStructure> GetAccelerationStructure() const;

private:
    void CreateTextureView();

    MTDevice& device_;
    MTResource* mt_resource_;
    id<MTLTexture> texture_view_ = nullptr;
};
