#include "View/DXView.h"

#include "Device/DXDevice.h"
#include "Utilities/Check.h"
#include "Utilities/Common.h"
#include "Utilities/DXGIFormatHelper.h"
#include "Utilities/NotReached.h"

#include <directx/d3d12.h>
#include <gli/gli.hpp>

#include <cassert>

DXView::DXView(DXDevice& device, const std::shared_ptr<DXResource>& resource, const ViewDesc& view_desc)
    : ViewBase(resource, view_desc)
    , device_(device)
    , dx_resource_(resource.get())
{
    if (view_desc_.view_type == ViewType::kShadingRateSource) {
        return;
    }

    handle_ = device_.GetCPUDescriptorPool().AllocateDescriptor(view_desc_.view_type);

    if (dx_resource_) {
        CreateView();
    }

    if (view_desc.bindless) {
        CreateBindlessTypedViewPool(device_);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE DXView::GetHandle()
{
    return handle_->GetCpuHandle();
}

void DXView::CreateView()
{
    switch (view_desc_.view_type) {
    case ViewType::kTexture:
    case ViewType::kBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kByteAddressBuffer:
        CreateSRV();
        break;
    case ViewType::kAccelerationStructure:
        CreateRAS();
        break;
    case ViewType::kRWTexture:
    case ViewType::kRWBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kRWByteAddressBuffer:
        CreateUAV();
        break;
    case ViewType::kConstantBuffer:
        CreateCBV();
        break;
    case ViewType::kSampler:
        CreateSampler();
        break;
    case ViewType::kRenderTarget:
        CreateRTV();
        break;
    case ViewType::kDepthStencil:
        CreateDSV();
        break;
    default:
        NOTREACHED();
    }
}

void DXView::CreateSRV()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = dx_resource_->GetResourceDesc().Format;

    if (IsTypelessDepthStencil(srv_desc.Format)) {
        if (view_desc_.plane_slice == 0) {
            srv_desc.Format = DepthReadFromTypeless(srv_desc.Format);
        } else {
            srv_desc.Format = StencilReadFromTypeless(srv_desc.Format);
        }
    }

    auto setup_mips = [&](uint32_t& most_detailed_mip, uint32_t& mip_levels) {
        most_detailed_mip = GetBaseMipLevel();
        mip_levels = GetLevelCount();
    };

    switch (view_desc_.dimension) {
    case ViewDimension::kTexture1D: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        setup_mips(srv_desc.Texture1D.MostDetailedMip, srv_desc.Texture1D.MipLevels);
        break;
    }
    case ViewDimension::kTexture1DArray: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        srv_desc.Texture1DArray.FirstArraySlice = GetBaseArrayLayer();
        srv_desc.Texture1DArray.ArraySize = GetLayerCount();
        setup_mips(srv_desc.Texture1DArray.MostDetailedMip, srv_desc.Texture1DArray.MipLevels);
        break;
    }
    case ViewDimension::kTexture2D: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.PlaneSlice = view_desc_.plane_slice;
        setup_mips(srv_desc.Texture2D.MostDetailedMip, srv_desc.Texture2D.MipLevels);
        break;
    }
    case ViewDimension::kTexture2DArray: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv_desc.Texture2DArray.PlaneSlice = view_desc_.plane_slice;
        srv_desc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
        srv_desc.Texture2DArray.ArraySize = GetLayerCount();
        setup_mips(srv_desc.Texture2DArray.MostDetailedMip, srv_desc.Texture2DArray.MipLevels);
        break;
    }
    case ViewDimension::kTexture2DMS: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        break;
    }
    case ViewDimension::kTexture2DMSArray: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        srv_desc.Texture2DMSArray.FirstArraySlice = GetBaseArrayLayer();
        srv_desc.Texture2DMSArray.ArraySize = GetLayerCount();
        break;
    }
    case ViewDimension::kTexture3D: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        setup_mips(srv_desc.Texture3D.MostDetailedMip, srv_desc.Texture3D.MipLevels);
        break;
    }
    case ViewDimension::kTextureCube: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        setup_mips(srv_desc.TextureCube.MostDetailedMip, srv_desc.TextureCube.MipLevels);
        break;
    }
    case ViewDimension::kTextureCubeArray: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        srv_desc.TextureCubeArray.First2DArrayFace = GetBaseArrayLayer() / 6;
        srv_desc.TextureCubeArray.NumCubes = GetLayerCount() / 6;
        setup_mips(srv_desc.TextureCubeArray.MostDetailedMip, srv_desc.TextureCubeArray.MipLevels);
        break;
    }
    case ViewDimension::kBuffer: {
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        uint32_t stride = 0;
        if (view_desc_.view_type == ViewType::kBuffer) {
            srv_desc.Format = static_cast<DXGI_FORMAT>(gli::dx().translate(view_desc_.buffer_format).DXGIFormat.DDS);
            stride = gli::detail::bits_per_pixel(view_desc_.buffer_format) / 8;
        } else if (view_desc_.view_type == ViewType::kStructuredBuffer) {
            srv_desc.Buffer.StructureByteStride = view_desc_.structure_stride;
            stride = srv_desc.Buffer.StructureByteStride;
        } else {
            assert(view_desc_.view_type == ViewType::kByteAddressBuffer);
            srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            stride = 4;
        }
        uint64_t size = std::min(dx_resource_->GetResourceDesc().Width - view_desc_.offset, view_desc_.buffer_size);
        DCHECK(view_desc_.offset % stride == 0);
        srv_desc.Buffer.FirstElement = view_desc_.offset / stride;
        srv_desc.Buffer.NumElements = size / stride;
        break;
    }
    default: {
        NOTREACHED();
    }
    }

    device_.GetDevice()->CreateShaderResourceView(dx_resource_->GetResource(), &srv_desc, handle_->GetCpuHandle());
}

void DXView::CreateRAS()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srv_desc.RaytracingAccelerationStructure.Location = dx_resource_->GetAccelerationStructureAddress();
    device_.GetDevice()->CreateShaderResourceView(nullptr, &srv_desc, handle_->GetCpuHandle());
}

void DXView::CreateUAV()
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = dx_resource_->GetResourceDesc().Format;

    switch (view_desc_.dimension) {
    case ViewDimension::kTexture1D: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
        uav_desc.Texture1D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture1DArray: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        uav_desc.Texture1DArray.FirstArraySlice = GetBaseArrayLayer();
        uav_desc.Texture1DArray.ArraySize = GetLayerCount();
        uav_desc.Texture1DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2D: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Texture2D.PlaneSlice = view_desc_.plane_slice;
        uav_desc.Texture2D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2DArray: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uav_desc.Texture2DArray.PlaneSlice = view_desc_.plane_slice;
        uav_desc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
        uav_desc.Texture2DArray.ArraySize = GetLayerCount();
        uav_desc.Texture2DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture3D: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uav_desc.Texture3D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kBuffer: {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uint32_t stride = 0;
        if (view_desc_.view_type == ViewType::kRWBuffer) {
            uav_desc.Format = static_cast<DXGI_FORMAT>(gli::dx().translate(view_desc_.buffer_format).DXGIFormat.DDS);
            stride = gli::detail::bits_per_pixel(view_desc_.buffer_format) / 8;
        } else if (view_desc_.view_type == ViewType::kRWStructuredBuffer) {
            uav_desc.Buffer.StructureByteStride = view_desc_.structure_stride;
            stride = uav_desc.Buffer.StructureByteStride;
        } else {
            assert(view_desc_.view_type == ViewType::kRWByteAddressBuffer);
            uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            stride = 4;
        }
        uint64_t size = std::min(dx_resource_->GetResourceDesc().Width - view_desc_.offset, view_desc_.buffer_size);
        DCHECK(view_desc_.offset % stride == 0);
        uav_desc.Buffer.FirstElement = view_desc_.offset / stride;
        uav_desc.Buffer.NumElements = size / stride;
        break;
    }
    default: {
        NOTREACHED();
    }
    }

    device_.GetDevice()->CreateUnorderedAccessView(dx_resource_->GetResource(), nullptr, &uav_desc,
                                                   handle_->GetCpuHandle());
}

void DXView::CreateRTV()
{
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = dx_resource_->GetResourceDesc().Format;

    switch (view_desc_.dimension) {
    case ViewDimension::kTexture1D: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
        rtv_desc.Texture1D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture1DArray: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        rtv_desc.Texture1DArray.FirstArraySlice = GetBaseArrayLayer();
        rtv_desc.Texture1DArray.ArraySize = GetLayerCount();
        rtv_desc.Texture1DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2D: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv_desc.Texture2D.PlaneSlice = view_desc_.plane_slice;
        rtv_desc.Texture2D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2DArray: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Texture2DArray.PlaneSlice = view_desc_.plane_slice;
        rtv_desc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
        rtv_desc.Texture2DArray.ArraySize = GetLayerCount();
        rtv_desc.Texture2DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2DMS: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        break;
    }
    case ViewDimension::kTexture2DMSArray: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rtv_desc.Texture2DMSArray.FirstArraySlice = GetBaseArrayLayer();
        rtv_desc.Texture2DMSArray.ArraySize = GetLayerCount();
        break;
    }
    case ViewDimension::kTexture3D: {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        rtv_desc.Texture3D.MipSlice = GetBaseMipLevel();
        break;
    }
    default: {
        NOTREACHED();
    }
    }

    device_.GetDevice()->CreateRenderTargetView(dx_resource_->GetResource(), &rtv_desc, handle_->GetCpuHandle());
}

void DXView::CreateDSV()
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format = DepthStencilFromTypeless(dx_resource_->GetResourceDesc().Format);

    switch (view_desc_.dimension) {
    case ViewDimension::kTexture1D: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
        dsv_desc.Texture1D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture1DArray: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        dsv_desc.Texture1DArray.FirstArraySlice = GetBaseArrayLayer();
        dsv_desc.Texture1DArray.ArraySize = GetLayerCount();
        dsv_desc.Texture1DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2D: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Texture2D.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2DArray: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_desc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
        dsv_desc.Texture2DArray.ArraySize = GetLayerCount();
        dsv_desc.Texture2DArray.MipSlice = GetBaseMipLevel();
        break;
    }
    case ViewDimension::kTexture2DMS: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        break;
    }
    case ViewDimension::kTexture2DMSArray: {
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        dsv_desc.Texture2DMSArray.FirstArraySlice = GetLayerCount();
        dsv_desc.Texture2DMSArray.ArraySize = GetLayerCount();
        break;
    }
    default: {
        NOTREACHED();
    }
    }

    device_.GetDevice()->CreateDepthStencilView(dx_resource_->GetResource(), &dsv_desc, handle_->GetCpuHandle());
}

void DXView::CreateCBV()
{
    CHECK(view_desc_.offset < dx_resource_->GetResourceDesc().Width);
    CHECK(view_desc_.offset % device_.GetConstantBufferOffsetAlignment() == 0);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cvb_desc = {};
    cvb_desc.BufferLocation = dx_resource_->GetResource()->GetGPUVirtualAddress() + view_desc_.offset;
    cvb_desc.SizeInBytes = std::min(dx_resource_->GetResourceDesc().Width - view_desc_.offset, view_desc_.buffer_size);
    cvb_desc.SizeInBytes = Align(cvb_desc.SizeInBytes, device_.GetConstantBufferOffsetAlignment());
    DCHECK(view_desc_.offset + cvb_desc.SizeInBytes <= dx_resource_->GetResourceDesc().Width);
    device_.GetDevice()->CreateConstantBufferView(&cvb_desc, handle_->GetCpuHandle());
}

void DXView::CreateSampler()
{
    device_.GetDevice()->CreateSampler(&dx_resource_->GetSamplerDesc(), handle_->GetCpuHandle());
}
