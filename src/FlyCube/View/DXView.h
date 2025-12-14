#pragma once
#include "CPUDescriptorPool/DXCPUDescriptorHandle.h"
#include "Resource/DXResource.h"
#include "View/ViewBase.h"

class DXDevice;
class DXResource;

class DXView : public ViewBase {
public:
    DXView(DXDevice& device, const std::shared_ptr<DXResource>& resource, const ViewDesc& view_desc);

    D3D12_CPU_DESCRIPTOR_HANDLE GetHandle();

private:
    void CreateView();
    void CreateSRV();
    void CreateRAS();
    void CreateUAV();
    void CreateRTV();
    void CreateDSV();
    void CreateCBV();
    void CreateSampler();

    DXDevice& device_;
    DXResource* dx_resource_;
    std::shared_ptr<DXCPUDescriptorHandle> handle_;
};
