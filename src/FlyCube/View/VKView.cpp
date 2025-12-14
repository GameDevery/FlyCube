#include "View/VKView.h"

#include "BindingSetLayout/VKBindingSetLayout.h"
#include "Device/VKDevice.h"
#include "Resource/VKResource.h"
#include "Utilities/NotReached.h"

namespace {

vk::ImageViewType GetImageViewType(ViewDimension dimension)
{
    switch (dimension) {
    case ViewDimension::kTexture1D:
        return vk::ImageViewType::e1D;
    case ViewDimension::kTexture1DArray:
        return vk::ImageViewType::e1DArray;
    case ViewDimension::kTexture2D:
    case ViewDimension::kTexture2DMS:
        return vk::ImageViewType::e2D;
    case ViewDimension::kTexture2DArray:
    case ViewDimension::kTexture2DMSArray:
        return vk::ImageViewType::e2DArray;
    case ViewDimension::kTexture3D:
        return vk::ImageViewType::e3D;
    case ViewDimension::kTextureCube:
        return vk::ImageViewType::eCube;
    case ViewDimension::kTextureCubeArray:
        return vk::ImageViewType::eCubeArray;
    default:
        NOTREACHED();
    }
}

} // namespace

VKView::VKView(VKDevice& device, const std::shared_ptr<VKResource>& resource, const ViewDesc& view_desc)
    : ViewBase(resource, view_desc)
    , device_(device)
    , vk_resource_(resource.get())
{
    if (resource) {
        CreateView();
    }

    if (view_desc.bindless) {
        CreateBindlessTypedViewPool(device_);
    }
}

void VKView::CreateView()
{
    switch (view_desc_.view_type) {
    case ViewType::kSampler:
        descriptor_image_.sampler = vk_resource_->GetSampler();
        descriptor_.pImageInfo = &descriptor_image_;
        break;
    case ViewType::kTexture: {
        CreateImageView();
        descriptor_image_.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        descriptor_image_.imageView = image_view_.get();
        descriptor_.pImageInfo = &descriptor_image_;
        break;
    }
    case ViewType::kRWTexture: {
        CreateImageView();
        descriptor_image_.imageLayout = vk::ImageLayout::eGeneral;
        descriptor_image_.imageView = image_view_.get();
        descriptor_.pImageInfo = &descriptor_image_;
        break;
    }
    case ViewType::kAccelerationStructure: {
        acceleration_structure_ = vk_resource_->GetAccelerationStructure();
        descriptor_acceleration_structure_.accelerationStructureCount = 1;
        descriptor_acceleration_structure_.pAccelerationStructures = &acceleration_structure_;
        descriptor_.pNext = &descriptor_acceleration_structure_;
        break;
    }
    case ViewType::kShadingRateSource:
    case ViewType::kRenderTarget:
    case ViewType::kDepthStencil: {
        CreateImageView();
        break;
    }
    case ViewType::kConstantBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kRWByteAddressBuffer: {
        uint64_t size = std::min(vk_resource_->GetWidth() - view_desc_.offset, view_desc_.buffer_size);
        descriptor_buffer_.buffer = vk_resource_->GetBuffer();
        descriptor_buffer_.offset = view_desc_.offset;
        descriptor_buffer_.range = size;
        descriptor_.pBufferInfo = &descriptor_buffer_;
        break;
    }
    case ViewType::kBuffer:
    case ViewType::kRWBuffer:
        CreateBufferView();
        descriptor_.pTexelBufferView = &buffer_view_.get();
        break;
    default:
        NOTREACHED();
    }
}

void VKView::CreateImageView()
{
    vk::ImageViewCreateInfo image_view_desc = {};
    image_view_desc.image = vk_resource_->GetImage();
    image_view_desc.format = static_cast<vk::Format>(vk_resource_->GetFormat());
    image_view_desc.viewType = GetImageViewType(view_desc_.dimension);
    image_view_desc.subresourceRange.baseMipLevel = GetBaseMipLevel();
    image_view_desc.subresourceRange.levelCount = GetLevelCount();
    image_view_desc.subresourceRange.baseArrayLayer = GetBaseArrayLayer();
    image_view_desc.subresourceRange.layerCount = GetLayerCount();
    image_view_desc.subresourceRange.aspectMask = device_.GetAspectFlags(image_view_desc.format);

    if (image_view_desc.subresourceRange.aspectMask &
        (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)) {
        if (view_desc_.plane_slice == 0) {
            image_view_desc.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        } else {
            image_view_desc.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
            image_view_desc.components.g = vk::ComponentSwizzle::eR;
        }
    }

    image_view_ = device_.GetDevice().createImageViewUnique(image_view_desc);
}

void VKView::CreateBufferView()
{
    uint64_t size = std::min(vk_resource_->GetWidth() - view_desc_.offset, view_desc_.buffer_size);
    vk::BufferViewCreateInfo buffer_view_desc = {};
    buffer_view_desc.buffer = vk_resource_->GetBuffer();
    buffer_view_desc.format = static_cast<vk::Format>(view_desc_.buffer_format);
    buffer_view_desc.offset = view_desc_.offset;
    buffer_view_desc.range = size;
    buffer_view_ = device_.GetDevice().createBufferViewUnique(buffer_view_desc);
}

vk::ImageView VKView::GetImageView() const
{
    return image_view_.get();
}

vk::WriteDescriptorSet VKView::GetDescriptor() const
{
    return descriptor_;
}
