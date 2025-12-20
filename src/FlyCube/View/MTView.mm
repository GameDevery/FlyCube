#include "View/MTView.h"

#include "Device/MTDevice.h"
#include "Memory/MTMemory.h"
#include "Resource/MTResource.h"
#include "Utilities/Common.h"
#include "Utilities/Logging.h"
#include "Utilities/NotReached.h"

#if defined(USE_METAL_SHADER_CONVERTER)
#include <metal_irconverter_runtime.h>
#endif

namespace {

MTLTextureType ConvertTextureType(ViewDimension dimension)
{
    switch (dimension) {
    case ViewDimension::kTexture1D:
        return MTLTextureType1D;
    case ViewDimension::kTexture1DArray:
        return MTLTextureType1DArray;
    case ViewDimension::kTexture2D:
        return MTLTextureType2D;
    case ViewDimension::kTexture2DMS:
        return MTLTextureType2DMultisample;
    case ViewDimension::kTexture2DArray:
        return MTLTextureType2DArray;
    case ViewDimension::kTexture2DMSArray:
        return MTLTextureType2DMultisampleArray;
    case ViewDimension::kTexture3D:
        return MTLTextureType3D;
    case ViewDimension::kTextureCube:
        return MTLTextureTypeCube;
    case ViewDimension::kTextureCubeArray:
        return MTLTextureTypeCubeArray;
    default:
        NOTREACHED();
    }
}

} // namespace

MTView::MTView(MTDevice& device, const std::shared_ptr<MTResource>& resource, const ViewDesc& view_desc)
    : ViewBase(resource, view_desc)
    , device_(device)
    , mt_resource_(resource.get())
{
    if (mt_resource_) {
        CreateView();
    }

    if (view_desc.bindless) {
        CreateBindlessTypedViewPool(device_);
    }
}

void MTView::CreateView()
{
    switch (view_desc_.view_type) {
    case ViewType::kTexture:
    case ViewType::kRWTexture:
        CreateTextureView();
        break;
    case ViewType::kBuffer:
    case ViewType::kRWBuffer:
        CreateTextureBufferView();
        break;
    case ViewType::kRenderTarget:
    case ViewType::kDepthStencil:
        texture_view_ = mt_resource_->GetTexture();
        break;
    default:
        break;
    }
}

void MTView::CreateTextureView()
{
    id<MTLTexture> texture = mt_resource_->GetTexture();
    MTLPixelFormat format = device_.GetMTLPixelFormat(mt_resource_->GetFormat());
    MTLTextureType texture_type = ConvertTextureType(view_desc_.dimension);
    NSRange levels = { GetBaseMipLevel(), GetLevelCount() };
    NSRange slices = { GetBaseArrayLayer(), GetLayerCount() };

    if (view_desc_.plane_slice == 1) {
        if (format == MTLPixelFormatDepth32Float_Stencil8) {
            format = MTLPixelFormatX32_Stencil8;
        }

        MTLTextureSwizzleChannels swizzle = MTLTextureSwizzleChannelsDefault;
        swizzle.green = MTLTextureSwizzleRed;

        texture_view_ = [texture newTextureViewWithPixelFormat:format
                                                   textureType:texture_type
                                                        levels:levels
                                                        slices:slices
                                                       swizzle:swizzle];
    } else {
        texture_view_ = [texture newTextureViewWithPixelFormat:format
                                                   textureType:texture_type
                                                        levels:levels
                                                        slices:slices];
    }

    if (!texture_view_) {
        Logging::Println("Failed to create MTLTexture using newTextureViewWithPixelFormat");
    }
}

void MTView::CreateTextureBufferView()
{
    id<MTLBuffer> buffer = mt_resource_->GetBuffer();
    MTLPixelFormat format = device_.GetMTLPixelFormat(view_desc_.buffer_format);
    uint32_t bits_per_pixel = gli::detail::bits_per_pixel(view_desc_.buffer_format) / 8;
    uint64_t size = std::min(mt_resource_->GetWidth() - view_desc_.offset, view_desc_.buffer_size);
    uint64_t width = size / bits_per_pixel;
    MTLResourceOptions options = ConvertStorageMode(mt_resource_->GetMemoryType()) << MTLResourceStorageModeShift;
    MTLTextureDescriptor* texture_descriptor =
        [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:format
                                                               width:width
                                                     resourceOptions:options
                                                               usage:MTLTextureUsageShaderRead];
    uint32_t alignment = [device_.GetDevice() minimumTextureBufferAlignmentForPixelFormat:format];
    texture_view_ = [buffer newTextureWithDescriptor:texture_descriptor
                                              offset:view_desc_.offset
                                         bytesPerRow:Align(bits_per_pixel * width, alignment)];
    if (!texture_view_) {
        Logging::Println("Failed to create MTLTexture using newTextureWithDescriptor from buffer");
    }
}

id<MTLTexture> MTView::GetTextureView() const
{
    return texture_view_;
}

id<MTLResource> MTView::GetAllocation() const
{
    if (!mt_resource_) {
        return nullptr;
    }

    switch (view_desc_.view_type) {
    case ViewType::kConstantBuffer:
    case ViewType::kBuffer:
    case ViewType::kRWBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kRWByteAddressBuffer:
        return mt_resource_->GetBuffer();
    case ViewType::kSampler:
        return nullptr;
    case ViewType::kTexture:
    case ViewType::kRWTexture:
        return mt_resource_->GetTexture();
    case ViewType::kAccelerationStructure:
        return mt_resource_->GetAccelerationStructure();
    default:
        NOTREACHED();
    }
}

MTLGPUAddress MTView::GetGpuAddress() const
{
    if (!mt_resource_) {
        return 0;
    }

    switch (view_desc_.view_type) {
    case ViewType::kConstantBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kRWByteAddressBuffer:
        return mt_resource_->GetBuffer().gpuAddress + view_desc_.offset;
    case ViewType::kSampler:
        return mt_resource_->GetSampler().gpuResourceID._impl;
    case ViewType::kTexture:
    case ViewType::kRWTexture:
    case ViewType::kBuffer:
    case ViewType::kRWBuffer:
        return texture_view_.gpuResourceID._impl;
    case ViewType::kAccelerationStructure:
        return mt_resource_->GetAccelerationStructure().gpuResourceID._impl;
    default:
        NOTREACHED();
    }
}

#if defined(USE_METAL_SHADER_CONVERTER)
void MTView::BindView(IRDescriptorTableEntry* entry)
{
    switch (view_desc_.view_type) {
    case ViewType::kSampler: {
        IRDescriptorTableSetSampler(entry, mt_resource_->GetSampler(), 0.0);
        break;
    }
    case ViewType::kTexture:
    case ViewType::kRWTexture:
    case ViewType::kBuffer:
    case ViewType::kRWBuffer: {
        IRDescriptorTableSetTexture(entry, texture_view_, 0.0, 0);
        break;
    }
    case ViewType::kConstantBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kRWByteAddressBuffer:
    case ViewType::kAccelerationStructure: {
        entry->gpuVA = GetGpuAddress();
        entry->textureViewID = 0;
        entry->metadata = 0;
        break;
    }
    default:
        NOTREACHED();
    }
}
#else
void MTView::BindView(id<MTL4ArgumentTable> argument_table, uint32_t index)
{
    MTLGPUAddress gpu_address = GetGpuAddress();
    switch (view_desc_.view_type) {
    case ViewType::kConstantBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kRWByteAddressBuffer:
        [argument_table setAddress:gpu_address atIndex:index];
        break;
    case ViewType::kSampler:
        [argument_table setSamplerState:{ gpu_address } atIndex:index];
        break;
    case ViewType::kTexture:
    case ViewType::kRWTexture:
    case ViewType::kBuffer:
    case ViewType::kRWBuffer:
        [argument_table setTexture:{ gpu_address } atIndex:index];
        break;
    case ViewType::kAccelerationStructure:
        [argument_table setResource:{ gpu_address } atBufferIndex:index];
        break;
    default:
        NOTREACHED();
    }
}
#endif
