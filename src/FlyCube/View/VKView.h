#pragma once
#include "Resource/VKResource.h"
#include "View/ViewBase.h"

class VKDevice;

class VKView : public ViewBase {
public:
    VKView(VKDevice& device, const std::shared_ptr<VKResource>& resource, const ViewDesc& view_desc);

    vk::ImageView GetImageView() const;
    vk::WriteDescriptorSet GetDescriptor() const;

private:
    void CreateView();
    void CreateImageView();
    void CreateBufferView();

    VKDevice& device_;
    VKResource* vk_resource_;
    vk::UniqueImageView image_view_;
    vk::UniqueBufferView buffer_view_;
    vk::DescriptorImageInfo descriptor_image_;
    vk::DescriptorBufferInfo descriptor_buffer_;
    vk::AccelerationStructureKHR acceleration_structure_;
    vk::WriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_;
    vk::WriteDescriptorSet descriptor_;
};
