#pragma once
#include "BindlessTypedViewPool/BindlessTypedViewPoolBase.h"
#include "Instance/BaseTypes.h"

#include <vulkan/vulkan.hpp>

class View;
class VKDevice;
class VKGPUDescriptorPoolRange;
class VKView;

class VKBindlessTypedViewPool : public BindlessTypedViewPoolBase {
public:
    VKBindlessTypedViewPool(VKDevice& device, ViewType view_type, uint32_t view_count);

    // BindlessTypedViewPool:
    uint32_t GetBaseDescriptorId() const override;
    uint32_t GetViewCount() const override;
    void WriteView(uint32_t index, const std::shared_ptr<View>& view) override;

    // BindlessTypedViewPoolBase:
    void WriteViewImpl(uint32_t index, View* view) override;

private:
    VKDevice& device_;

    uint32_t view_count_;
    vk::DescriptorType descriptor_type_;
    std::shared_ptr<VKGPUDescriptorPoolRange> range_;
};
