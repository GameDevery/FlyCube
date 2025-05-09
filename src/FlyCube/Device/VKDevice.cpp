#include "Device/VKDevice.h"

#include "Adapter/VKAdapter.h"
#include "BindingSet/VKBindingSet.h"
#include "BindingSetLayout/VKBindingSetLayout.h"
#include "CommandList/VKCommandList.h"
#include "CommandQueue/VKCommandQueue.h"
#include "Fence/VKTimelineSemaphore.h"
#include "Framebuffer/VKFramebuffer.h"
#include "Instance/VKInstance.h"
#include "Memory/VKMemory.h"
#include "Pipeline/VKComputePipeline.h"
#include "Pipeline/VKGraphicsPipeline.h"
#include "Pipeline/VKRayTracingPipeline.h"
#include "Program/ProgramBase.h"
#include "QueryHeap/VKQueryHeap.h"
#include "RenderPass/VKRenderPass.h"
#include "Shader/ShaderBase.h"
#include "Swapchain/VKSwapchain.h"
#include "Utilities/VKUtility.h"
#include "View/VKView.h"

#include <set>
#include <string_view>

namespace {

vk::IndexType GetVkIndexType(gli::format format)
{
    vk::Format vk_format = static_cast<vk::Format>(format);
    switch (vk_format) {
    case vk::Format::eR16Uint:
        return vk::IndexType::eUint16;
    case vk::Format::eR32Uint:
        return vk::IndexType::eUint32;
    default:
        assert(false);
        return {};
    }
}

vk::AccelerationStructureTypeKHR Convert(AccelerationStructureType type)
{
    switch (type) {
    case AccelerationStructureType::kTopLevel:
        return vk::AccelerationStructureTypeKHR::eTopLevel;
    case AccelerationStructureType::kBottomLevel:
        return vk::AccelerationStructureTypeKHR::eBottomLevel;
    }
    assert(false);
    return {};
}

std::pair<uint32_t, uint32_t> ConvertShadingRateToPair(ShadingRate shading_rate)
{
    vk::Extent2D size = ConvertShadingRate(shading_rate);
    return { size.width, size.height };
}

} // namespace

vk::ImageLayout ConvertState(ResourceState state)
{
    static std::pair<ResourceState, vk::ImageLayout> mapping[] = {
        { ResourceState::kCommon, vk::ImageLayout::eGeneral },
        { ResourceState::kRenderTarget, vk::ImageLayout::eColorAttachmentOptimal },
        { ResourceState::kUnorderedAccess, vk::ImageLayout::eGeneral },
        { ResourceState::kDepthStencilWrite, vk::ImageLayout::eDepthStencilAttachmentOptimal },
        { ResourceState::kDepthStencilRead, vk::ImageLayout::eDepthStencilReadOnlyOptimal },
        { ResourceState::kNonPixelShaderResource, vk::ImageLayout::eShaderReadOnlyOptimal },
        { ResourceState::kPixelShaderResource, vk::ImageLayout::eShaderReadOnlyOptimal },
        { ResourceState::kCopyDest, vk::ImageLayout::eTransferDstOptimal },
        { ResourceState::kCopySource, vk::ImageLayout::eTransferSrcOptimal },
        { ResourceState::kShadingRateSource, vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR },
        { ResourceState::kPresent, vk::ImageLayout::ePresentSrcKHR },
        { ResourceState::kUndefined, vk::ImageLayout::eUndefined },
    };
    for (const auto& m : mapping) {
        if (state & m.first) {
            assert(state == m.first);
            return m.second;
        }
    }
    assert(false);
    return vk::ImageLayout::eGeneral;
}

vk::BuildAccelerationStructureFlagsKHR Convert(BuildAccelerationStructureFlags flags)
{
    vk::BuildAccelerationStructureFlagsKHR vk_flags = {};
    if (flags & BuildAccelerationStructureFlags::kAllowUpdate) {
        vk_flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
    }
    if (flags & BuildAccelerationStructureFlags::kAllowCompaction) {
        vk_flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
    }
    if (flags & BuildAccelerationStructureFlags::kPreferFastTrace) {
        vk_flags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    }
    if (flags & BuildAccelerationStructureFlags::kPreferFastBuild) {
        vk_flags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild;
    }
    if (flags & BuildAccelerationStructureFlags::kMinimizeMemory) {
        vk_flags |= vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory;
    }
    return vk_flags;
}

vk::Extent2D ConvertShadingRate(ShadingRate shading_rate)
{
    vk::Extent2D fragment_size = { 1, 1 };
    switch (shading_rate) {
    case ShadingRate::k1x1:
        fragment_size.width = 1;
        fragment_size.height = 1;
        break;
    case ShadingRate::k1x2:
        fragment_size.width = 1;
        fragment_size.height = 2;
        break;
    case ShadingRate::k2x1:
        fragment_size.width = 2;
        fragment_size.height = 1;
        break;
    case ShadingRate::k2x2:
        fragment_size.width = 2;
        fragment_size.height = 2;
        break;
    case ShadingRate::k2x4:
        fragment_size.width = 2;
        fragment_size.height = 4;
        break;
    case ShadingRate::k4x2:
        fragment_size.width = 4;
        fragment_size.height = 2;
        break;
    case ShadingRate::k4x4:
        fragment_size.width = 4;
        fragment_size.height = 4;
        break;
    default:
        assert(false);
        break;
    }
    return fragment_size;
}

std::array<vk::FragmentShadingRateCombinerOpKHR, 2> ConvertShadingRateCombiners(
    const std::array<ShadingRateCombiner, 2>& combiners)
{
    std::array<vk::FragmentShadingRateCombinerOpKHR, 2> vk_combiners = {};
    for (size_t i = 0; i < vk_combiners.size(); ++i) {
        switch (combiners[i]) {
        case ShadingRateCombiner::kPassthrough:
            vk_combiners[i] = vk::FragmentShadingRateCombinerOpKHR::eKeep;
            break;
        case ShadingRateCombiner::kOverride:
            vk_combiners[i] = vk::FragmentShadingRateCombinerOpKHR::eReplace;
            break;
        case ShadingRateCombiner::kMin:
            vk_combiners[i] = vk::FragmentShadingRateCombinerOpKHR::eMin;
            break;
        case ShadingRateCombiner::kMax:
            vk_combiners[i] = vk::FragmentShadingRateCombinerOpKHR::eMax;
            break;
        case ShadingRateCombiner::kSum:
            vk_combiners[i] = vk::FragmentShadingRateCombinerOpKHR::eMul;
            break;
        default:
            assert(false);
            break;
        }
    }
    return vk_combiners;
}

VKDevice::VKDevice(VKAdapter& adapter)
    : m_adapter(adapter)
    , m_physical_device(adapter.GetPhysicalDevice())
    , m_gpu_descriptor_pool(*this)
{
    m_device_properties = m_physical_device.getProperties();
    auto physical_device_features = m_physical_device.getFeatures();
    auto queue_families = m_physical_device.getQueueFamilyProperties();
    auto has_all_bits = [](auto flags, auto bits) { return (flags & bits) == bits; };
    auto has_any_bits = [](auto flags, auto bits) { return flags & bits; };
    for (size_t i = 0; i < queue_families.size(); ++i) {
        const auto& queue = queue_families[i];
        if (queue.queueCount > 0 &&
            has_all_bits(queue.queueFlags,
                         vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer)) {
            m_queues_info[CommandListType::kGraphics].queue_family_index = i;
            m_queues_info[CommandListType::kGraphics].queue_count = queue.queueCount;
        } else if (queue.queueCount > 0 &&
                   has_all_bits(queue.queueFlags, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) &&
                   !has_any_bits(queue.queueFlags, vk::QueueFlagBits::eGraphics)) {
            m_queues_info[CommandListType::kCompute].queue_family_index = i;
            m_queues_info[CommandListType::kCompute].queue_count = queue.queueCount;
        } else if (queue.queueCount > 0 && has_all_bits(queue.queueFlags, vk::QueueFlagBits::eTransfer) &&
                   !has_any_bits(queue.queueFlags, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)) {
            m_queues_info[CommandListType::kCopy].queue_family_index = i;
            m_queues_info[CommandListType::kCopy].queue_count = queue.queueCount;
        }
    }

    auto extensions = m_physical_device.enumerateDeviceExtensionProperties();
    std::set<std::string_view> requested_extensions = {
        // clang-format off
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        // clang-format on
    };

    m_is_at_least_vulkan12 = m_device_properties.apiVersion >= VK_API_VERSION_1_2 &&
                             m_adapter.GetInstance().GetApiVersion() >= VK_API_VERSION_1_2;
    if (!m_is_at_least_vulkan12) {
        requested_extensions.insert(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        requested_extensions.insert(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }

    std::vector<const char*> enabled_extensions;
    std::set<std::string_view> enabled_extension_set;
    for (const auto& extension : extensions) {
        if (requested_extensions.contains(extension.extensionName.data())) {
            enabled_extensions.push_back(extension.extensionName.data());
            enabled_extension_set.insert(extension.extensionName.data());
        }
    }

    void* device_create_info_next = nullptr;
    auto add_extension = [&](auto& extension) {
        extension.pNext = device_create_info_next;
        device_create_info_next = &extension;
    };

    vk::PhysicalDeviceFragmentShadingRateFeaturesKHR fragment_shading_rate_features = {};
    if (enabled_extension_set.contains(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
        auto query_fragment_shading_rate_features = GetFeatures2<vk::PhysicalDeviceFragmentShadingRateFeaturesKHR>();
        if (query_fragment_shading_rate_features.pipelineFragmentShadingRate) {
            m_is_variable_rate_shading_supported = true;
            fragment_shading_rate_features.pipelineFragmentShadingRate = true;
        }

        if (query_fragment_shading_rate_features.attachmentFragmentShadingRate) {
            std::map<std::pair<uint32_t, uint32_t>, ShadingRate> expected_shading_rates = {
                { ConvertShadingRateToPair(ShadingRate::k1x1), ShadingRate::k1x1 },
                { ConvertShadingRateToPair(ShadingRate::k1x2), ShadingRate::k1x2 },
                { ConvertShadingRateToPair(ShadingRate::k2x1), ShadingRate::k2x1 },
                { ConvertShadingRateToPair(ShadingRate::k2x2), ShadingRate::k2x2 },
                { ConvertShadingRateToPair(ShadingRate::k2x4), ShadingRate::k2x4 },
                { ConvertShadingRateToPair(ShadingRate::k4x2), ShadingRate::k4x2 },
                { ConvertShadingRateToPair(ShadingRate::k4x4), ShadingRate::k4x4 },
            };
            decltype(auto) fragment_shading_rates = m_physical_device.getFragmentShadingRatesKHR();
            for (const auto& fragment_shading_rate : fragment_shading_rates) {
                vk::Extent2D size = fragment_shading_rate.fragmentSize;
                std::pair<uint32_t, uint32_t> size_as_pair = { size.width, size.height };
                if (!expected_shading_rates.contains(size_as_pair)) {
                    continue;
                }
                uint8_t shading_rate_bits = ((size.width >> 1) << 2) | (size.height >> 1);
                assert((1 << ((shading_rate_bits >> 2) & 3)) == size.width);
                assert((1 << (shading_rate_bits & 3)) == size.height);
                ShadingRate shading_rate = static_cast<ShadingRate>(shading_rate_bits);
                assert(expected_shading_rates.at(size_as_pair) == shading_rate);
                expected_shading_rates.erase(size_as_pair);
            }
            assert(expected_shading_rates.empty());

            vk::PhysicalDeviceFragmentShadingRatePropertiesKHR query_shading_rate_image_properties = {};
            vk::PhysicalDeviceProperties2 device_props2 = {};
            device_props2.pNext = &query_shading_rate_image_properties;
            m_physical_device.getProperties2(&device_props2);
            assert(query_shading_rate_image_properties.minFragmentShadingRateAttachmentTexelSize ==
                   query_shading_rate_image_properties.maxFragmentShadingRateAttachmentTexelSize);
            assert(query_shading_rate_image_properties.minFragmentShadingRateAttachmentTexelSize.width ==
                   query_shading_rate_image_properties.minFragmentShadingRateAttachmentTexelSize.height);
            assert(query_shading_rate_image_properties.maxFragmentShadingRateAttachmentTexelSize.width ==
                   query_shading_rate_image_properties.maxFragmentShadingRateAttachmentTexelSize.height);
            m_shading_rate_image_tile_size =
                query_shading_rate_image_properties.maxFragmentShadingRateAttachmentTexelSize.width;
            assert(m_shading_rate_image_tile_size != 0);

            fragment_shading_rate_features.attachmentFragmentShadingRate = true;
        }

        add_extension(fragment_shading_rate_features);
    }

    const float queue_priority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queues_create_info;
    for (const auto& queue_info : m_queues_info) {
        vk::DeviceQueueCreateInfo& queue_create_info = queues_create_info.emplace_back();
        queue_create_info.queueFamilyIndex = queue_info.second.queue_family_index;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
    }

    vk::PhysicalDeviceFeatures device_features = {};
    device_features.textureCompressionBC = physical_device_features.textureCompressionBC;
    device_features.vertexPipelineStoresAndAtomics = physical_device_features.vertexPipelineStoresAndAtomics;
    device_features.samplerAnisotropy = physical_device_features.samplerAnisotropy;
    device_features.fragmentStoresAndAtomics = physical_device_features.fragmentStoresAndAtomics;
    device_features.sampleRateShading = physical_device_features.sampleRateShading;
    device_features.geometryShader = physical_device_features.geometryShader;
    device_features.imageCubeArray = physical_device_features.imageCubeArray;
    device_features.shaderImageGatherExtended = physical_device_features.shaderImageGatherExtended;

    vk::PhysicalDeviceVulkan12Features device_vulkan12_features = {};
    vk::PhysicalDeviceDescriptorIndexingFeaturesEXT device_descriptor_indexing = {};
    vk::PhysicalDeviceBufferDeviceAddressFeaturesKHR device_buffer_device_address = {};
    vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR device_timeline_semaphore = {};
    if (m_is_at_least_vulkan12) {
        auto query_device_vulkan12_features = GetFeatures2<vk::PhysicalDeviceVulkan12Features>();
        device_vulkan12_features.drawIndirectCount = query_device_vulkan12_features.drawIndirectCount;
        device_vulkan12_features.bufferDeviceAddress = query_device_vulkan12_features.bufferDeviceAddress;
        device_vulkan12_features.timelineSemaphore = true;
        device_vulkan12_features.descriptorIndexing = query_device_vulkan12_features.descriptorIndexing;
        device_vulkan12_features.runtimeDescriptorArray = query_device_vulkan12_features.runtimeDescriptorArray;
        device_vulkan12_features.descriptorBindingPartiallyBound =
            query_device_vulkan12_features.descriptorBindingPartiallyBound;
        device_vulkan12_features.descriptorBindingVariableDescriptorCount =
            query_device_vulkan12_features.descriptorBindingVariableDescriptorCount;
        if (enabled_extension_set.contains(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME)) {
            device_vulkan12_features.shaderOutputLayer = true;
            device_vulkan12_features.shaderOutputViewportIndex = true;
        }
        add_extension(device_vulkan12_features);
    } else {
        if (enabled_extension_set.contains(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)) {
            auto query_descriptor_indexing = GetFeatures2<vk::PhysicalDeviceDescriptorIndexingFeaturesEXT>();
            device_descriptor_indexing.runtimeDescriptorArray = query_descriptor_indexing.runtimeDescriptorArray;
            device_descriptor_indexing.descriptorBindingPartiallyBound =
                query_descriptor_indexing.descriptorBindingPartiallyBound;
            device_descriptor_indexing.descriptorBindingVariableDescriptorCount =
                query_descriptor_indexing.descriptorBindingVariableDescriptorCount;
            add_extension(device_descriptor_indexing);
        }
        if (enabled_extension_set.contains(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
            auto query_device_buffer_device_address = GetFeatures2<vk::PhysicalDeviceBufferDeviceAddressFeaturesKHR>();
            device_buffer_device_address.bufferDeviceAddress = query_device_buffer_device_address.bufferDeviceAddress;
            add_extension(device_buffer_device_address);
        }
        assert(enabled_extension_set.contains(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME));
        device_timeline_semaphore.timelineSemaphore = true;
        add_extension(device_timeline_semaphore);
        assert(enabled_extension_set.contains(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME));
    }

    m_geometry_shader_supported = device_features.geometryShader;
    m_draw_indirect_count_supported = device_vulkan12_features.drawIndirectCount ||
                                      enabled_extension_set.contains(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
    m_has_buffer_device_address =
        device_vulkan12_features.bufferDeviceAddress || device_buffer_device_address.bufferDeviceAddress;

    vk::PhysicalDeviceMeshShaderFeaturesEXT mesh_shader_feature = {};
    if (enabled_extension_set.contains(VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
        auto query_mesh_shader_feature = GetFeatures2<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
        mesh_shader_feature.taskShader = query_mesh_shader_feature.taskShader;
        mesh_shader_feature.meshShader = query_mesh_shader_feature.meshShader;
        m_is_mesh_shading_supported = mesh_shader_feature.taskShader && mesh_shader_feature.meshShader;
        add_extension(mesh_shader_feature);
    }

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_feature = {};
    if (enabled_extension_set.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
        auto query_acceleration_structure_feature = GetFeatures2<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        acceleration_structure_feature.accelerationStructure =
            query_acceleration_structure_feature.accelerationStructure;
        add_extension(acceleration_structure_feature);
    }

    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR raytracing_pipeline_feature = {};
    if (enabled_extension_set.contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
        acceleration_structure_feature.accelerationStructure) {
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties = {};
        vk::PhysicalDeviceProperties2 device_props2 = {};
        device_props2.pNext = &ray_tracing_properties;
        m_physical_device.getProperties2(&device_props2);
        m_shader_group_handle_size = ray_tracing_properties.shaderGroupHandleSize;
        m_shader_record_alignment = ray_tracing_properties.shaderGroupHandleSize;
        m_shader_table_alignment = ray_tracing_properties.shaderGroupBaseAlignment;

        auto query_raytracing_pipeline_feature = GetFeatures2<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
        raytracing_pipeline_feature.rayTracingPipeline = query_raytracing_pipeline_feature.rayTracingPipeline;
        raytracing_pipeline_feature.rayTraversalPrimitiveCulling =
            query_raytracing_pipeline_feature.rayTraversalPrimitiveCulling;
        m_is_dxr_supported =
            raytracing_pipeline_feature.rayTracingPipeline && raytracing_pipeline_feature.rayTraversalPrimitiveCulling;
        add_extension(raytracing_pipeline_feature);
    }

    vk::PhysicalDeviceRayQueryFeaturesKHR rayquery_feature = {};
    if (enabled_extension_set.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
        acceleration_structure_feature.accelerationStructure) {
        auto query_rayquery_feature = GetFeatures2<vk::PhysicalDeviceRayQueryFeaturesKHR>();
        rayquery_feature.rayQuery = query_rayquery_feature.rayQuery;
        m_is_ray_query_supported = rayquery_feature.rayQuery;
        add_extension(rayquery_feature);
    }

    vk::DeviceCreateInfo device_create_info = {};
    device_create_info.pNext = device_create_info_next;
    device_create_info.queueCreateInfoCount = queues_create_info.size();
    device_create_info.pQueueCreateInfos = queues_create_info.data();
    device_create_info.pEnabledFeatures = &device_features;
    device_create_info.enabledExtensionCount = enabled_extensions.size();
    device_create_info.ppEnabledExtensionNames = enabled_extensions.data();

    m_device = m_physical_device.createDeviceUnique(device_create_info);

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device.get());
#endif

    for (auto& queue_info : m_queues_info) {
        vk::CommandPoolCreateInfo cmd_pool_create_info = {};
        cmd_pool_create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        cmd_pool_create_info.queueFamilyIndex = queue_info.second.queue_family_index;
        m_cmd_pools.emplace(queue_info.first, m_device->createCommandPoolUnique(cmd_pool_create_info));
        m_command_queues[queue_info.first] =
            std::make_shared<VKCommandQueue>(*this, queue_info.first, queue_info.second.queue_family_index);
    }
}

std::shared_ptr<Memory> VKDevice::AllocateMemory(uint64_t size, MemoryType memory_type, uint32_t memory_type_bits)
{
    return std::make_shared<VKMemory>(*this, size, memory_type, memory_type_bits, nullptr);
}

std::shared_ptr<CommandQueue> VKDevice::GetCommandQueue(CommandListType type)
{
    return m_command_queues.at(GetAvailableCommandListType(type));
}

uint32_t VKDevice::GetTextureDataPitchAlignment() const
{
    return 1;
}

std::shared_ptr<Swapchain> VKDevice::CreateSwapchain(WindowHandle window,
                                                     uint32_t width,
                                                     uint32_t height,
                                                     uint32_t frame_count,
                                                     bool vsync)
{
    return std::make_shared<VKSwapchain>(*m_command_queues.at(CommandListType::kGraphics), window, width, height,
                                         frame_count, vsync);
}

std::shared_ptr<CommandList> VKDevice::CreateCommandList(CommandListType type)
{
    return std::make_shared<VKCommandList>(*this, type);
}

std::shared_ptr<Fence> VKDevice::CreateFence(uint64_t initial_value)
{
    return std::make_shared<VKTimelineSemaphore>(*this, initial_value);
}

std::shared_ptr<Resource> VKDevice::CreateTexture(TextureType type,
                                                  uint32_t bind_flag,
                                                  gli::format format,
                                                  uint32_t sample_count,
                                                  int width,
                                                  int height,
                                                  int depth,
                                                  int mip_levels)
{
    std::shared_ptr<VKResource> res = std::make_shared<VKResource>(*this);
    res->format = format;
    res->resource_type = ResourceType::kTexture;
    res->image.size.height = height;
    res->image.size.width = width;
    res->image.format = static_cast<vk::Format>(format);
    res->image.level_count = mip_levels;
    res->image.sample_count = sample_count;
    res->image.array_layers = depth;

    vk::ImageUsageFlags usage = {};
    if (bind_flag & BindFlag::kDepthStencil) {
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst;
    }
    if (bind_flag & BindFlag::kShaderResource) {
        usage |= vk::ImageUsageFlagBits::eSampled;
    }
    if (bind_flag & BindFlag::kRenderTarget) {
        usage |= vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
    }
    if (bind_flag & BindFlag::kUnorderedAccess) {
        usage |= vk::ImageUsageFlagBits::eStorage;
    }
    if (bind_flag & BindFlag::kCopyDest) {
        usage |= vk::ImageUsageFlagBits::eTransferDst;
    }
    if (bind_flag & BindFlag::kCopySource) {
        usage |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    if (bind_flag & BindFlag::kShadingRateSource) {
        usage |= vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;
    }

    vk::ImageCreateInfo image_info = {};
    switch (type) {
    case TextureType::k1D:
        image_info.imageType = vk::ImageType::e1D;
        break;
    case TextureType::k2D:
        image_info.imageType = vk::ImageType::e2D;
        break;
    case TextureType::k3D:
        image_info.imageType = vk::ImageType::e3D;
        break;
    }
    image_info.extent.width = width;
    image_info.extent.height = height;
    if (type == TextureType::k3D) {
        image_info.extent.depth = depth;
    } else {
        image_info.extent.depth = 1;
    }
    image_info.mipLevels = mip_levels;
    if (type == TextureType::k3D) {
        image_info.arrayLayers = 1;
    } else {
        image_info.arrayLayers = depth;
    }
    image_info.format = res->image.format;
    image_info.tiling = vk::ImageTiling::eOptimal;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage = usage;
    image_info.samples = static_cast<vk::SampleCountFlagBits>(sample_count);
    image_info.sharingMode = vk::SharingMode::eExclusive;

    if (image_info.arrayLayers % 6 == 0) {
        image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }

    res->image.res_owner = m_device->createImageUnique(image_info);
    res->image.res = res->image.res_owner.get();

    res->SetInitialState(ResourceState::kUndefined);

    return res;
}

std::shared_ptr<Resource> VKDevice::CreateBuffer(uint32_t bind_flag, uint32_t buffer_size)
{
    if (buffer_size == 0) {
        return {};
    }

    std::shared_ptr<VKResource> res = std::make_shared<VKResource>(*this);
    res->resource_type = ResourceType::kBuffer;
    res->buffer.size = buffer_size;

    vk::BufferCreateInfo buffer_info = {};
    buffer_info.size = buffer_size;
    buffer_info.usage = vk::BufferUsageFlagBits::eShaderDeviceAddress;

    if (bind_flag & BindFlag::kVertexBuffer) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eVertexBuffer;
    }
    if (bind_flag & BindFlag::kIndexBuffer) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eIndexBuffer;
    }
    if (bind_flag & BindFlag::kConstantBuffer) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    }
    if (bind_flag & BindFlag::kUnorderedAccess) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        buffer_info.usage |= vk::BufferUsageFlagBits::eStorageTexelBuffer;
    }
    if (bind_flag & BindFlag::kShaderResource) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        buffer_info.usage |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
    }
    if (bind_flag & BindFlag::kAccelerationStructure) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
    }
    if (bind_flag & BindFlag::kCopySource) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eTransferSrc;
    }
    if (bind_flag & BindFlag::kCopyDest) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eTransferDst;
    }
    if (bind_flag & BindFlag::kShaderTable) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;
    }
    if (bind_flag & BindFlag::kIndirectBuffer) {
        buffer_info.usage |= vk::BufferUsageFlagBits::eIndirectBuffer;
    }

    res->buffer.res = m_device->createBufferUnique(buffer_info);
    res->SetInitialState(ResourceState::kCommon);

    return res;
}

std::shared_ptr<Resource> VKDevice::CreateSampler(const SamplerDesc& desc)
{
    std::shared_ptr<VKResource> res = std::make_shared<VKResource>(*this);

    vk::SamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.anisotropyEnable = true;
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = std::numeric_limits<float>::max();

    /*switch (desc.filter)
    {
    case SamplerFilter::kAnisotropic:
        sampler_desc.Filter = D3D12_FILTER_ANISOTROPIC;
        break;
    case SamplerFilter::kMinMagMipLinear:
        sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        break;
    case SamplerFilter::kComparisonMinMagMipLinear:
        sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        break;
    }*/

    switch (desc.mode) {
    case SamplerTextureAddressMode::kWrap:
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        break;
    case SamplerTextureAddressMode::kClamp:
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        break;
    }

    switch (desc.func) {
    case SamplerComparisonFunc::kNever:
        samplerInfo.compareEnable = false;
        samplerInfo.compareOp = vk::CompareOp::eNever;
        break;
    case SamplerComparisonFunc::kAlways:
        samplerInfo.compareEnable = true;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        break;
    case SamplerComparisonFunc::kLess:
        samplerInfo.compareEnable = true;
        samplerInfo.compareOp = vk::CompareOp::eLess;
        break;
    }

    res->sampler.res = m_device->createSamplerUnique(samplerInfo);

    res->resource_type = ResourceType::kSampler;
    return res;
}

std::shared_ptr<View> VKDevice::CreateView(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc)
{
    return std::make_shared<VKView>(*this, std::static_pointer_cast<VKResource>(resource), view_desc);
}

std::shared_ptr<BindingSetLayout> VKDevice::CreateBindingSetLayout(const std::vector<BindKey>& descs)
{
    return std::make_shared<VKBindingSetLayout>(*this, descs);
}

std::shared_ptr<BindingSet> VKDevice::CreateBindingSet(const std::shared_ptr<BindingSetLayout>& layout)
{
    return std::make_shared<VKBindingSet>(*this, std::static_pointer_cast<VKBindingSetLayout>(layout));
}

std::shared_ptr<RenderPass> VKDevice::CreateRenderPass(const RenderPassDesc& desc)
{
    return std::make_shared<VKRenderPass>(*this, desc);
}

std::shared_ptr<Framebuffer> VKDevice::CreateFramebuffer(const FramebufferDesc& desc)
{
    return std::make_shared<VKFramebuffer>(*this, desc);
}

std::shared_ptr<Shader> VKDevice::CreateShader(const std::vector<uint8_t>& blob,
                                               ShaderBlobType blob_type,
                                               ShaderType shader_type)
{
    return std::make_shared<ShaderBase>(blob, blob_type, shader_type);
}

std::shared_ptr<Shader> VKDevice::CompileShader(const ShaderDesc& desc)
{
    return std::make_shared<ShaderBase>(desc, ShaderBlobType::kSPIRV);
}

std::shared_ptr<Program> VKDevice::CreateProgram(const std::vector<std::shared_ptr<Shader>>& shaders)
{
    return std::make_shared<ProgramBase>(shaders);
}

std::shared_ptr<Pipeline> VKDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    return std::make_shared<VKGraphicsPipeline>(*this, desc);
}

std::shared_ptr<Pipeline> VKDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    return std::make_shared<VKComputePipeline>(*this, desc);
}

std::shared_ptr<Pipeline> VKDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
{
    return std::make_shared<VKRayTracingPipeline>(*this, desc);
}

vk::AccelerationStructureGeometryKHR VKDevice::FillRaytracingGeometryTriangles(const BufferDesc& vertex,
                                                                               const BufferDesc& index,
                                                                               RaytracingGeometryFlags flags) const
{
    vk::AccelerationStructureGeometryKHR geometry_desc = {};
    geometry_desc.geometryType = vk::GeometryTypeKHR::eTriangles;
    switch (flags) {
    case RaytracingGeometryFlags::kOpaque:
        geometry_desc.flags = vk::GeometryFlagBitsKHR::eOpaque;
        break;
    case RaytracingGeometryFlags::kNoDuplicateAnyHitInvocation:
        geometry_desc.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
        break;
    default:
        break;
    }

    auto vk_vertex_res = std::static_pointer_cast<VKResource>(vertex.res);
    auto vk_index_res = std::static_pointer_cast<VKResource>(index.res);

    auto vertex_stride = gli::detail::bits_per_pixel(vertex.format) / 8;
    geometry_desc.geometry.triangles.vertexData =
        m_device->getBufferAddress({ vk_vertex_res->buffer.res.get() }) + vertex.offset * vertex_stride;
    geometry_desc.geometry.triangles.vertexStride = vertex_stride;
    geometry_desc.geometry.triangles.vertexFormat = static_cast<vk::Format>(vertex.format);
    geometry_desc.geometry.triangles.maxVertex = vertex.count;
    if (vk_index_res) {
        auto index_stride = gli::detail::bits_per_pixel(index.format) / 8;
        geometry_desc.geometry.triangles.indexData =
            m_device->getBufferAddress({ vk_index_res->buffer.res.get() }) + index.offset * index_stride;
        geometry_desc.geometry.triangles.indexType = GetVkIndexType(index.format);
    } else {
        geometry_desc.geometry.triangles.indexType = vk::IndexType::eNoneKHR;
    }

    return geometry_desc;
}

RaytracingASPrebuildInfo VKDevice::GetAccelerationStructurePrebuildInfo(
    const vk::AccelerationStructureBuildGeometryInfoKHR& acceleration_structure_info,
    const std::vector<uint32_t>& max_primitive_counts) const
{
    vk::AccelerationStructureBuildSizesInfoKHR size_info = {};
    m_device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
                                                    &acceleration_structure_info, max_primitive_counts.data(),
                                                    &size_info);
    RaytracingASPrebuildInfo prebuild_info = {};
    prebuild_info.acceleration_structure_size = size_info.accelerationStructureSize;
    prebuild_info.build_scratch_data_size = size_info.buildScratchSize;
    prebuild_info.update_scratch_data_size = size_info.updateScratchSize;
    return prebuild_info;
}

std::shared_ptr<Resource> VKDevice::CreateAccelerationStructure(AccelerationStructureType type,
                                                                const std::shared_ptr<Resource>& resource,
                                                                uint64_t offset)
{
    std::shared_ptr<VKResource> res = std::make_shared<VKResource>(*this);
    res->resource_type = ResourceType::kAccelerationStructure;
    res->acceleration_structures_memory = resource;

    vk::AccelerationStructureCreateInfoKHR acceleration_structure_create_info = {};
    acceleration_structure_create_info.buffer = resource->As<VKResource>().buffer.res.get();
    acceleration_structure_create_info.offset = offset;
    acceleration_structure_create_info.size = 0;
    acceleration_structure_create_info.type = Convert(type);
    res->acceleration_structure_handle =
        m_device->createAccelerationStructureKHRUnique(acceleration_structure_create_info);

    return res;
}

std::shared_ptr<QueryHeap> VKDevice::CreateQueryHeap(QueryHeapType type, uint32_t count)
{
    return std::make_shared<VKQueryHeap>(*this, type, count);
}

bool VKDevice::IsDxrSupported() const
{
    return m_is_dxr_supported;
}

bool VKDevice::IsRayQuerySupported() const
{
    return m_is_ray_query_supported;
}

bool VKDevice::IsVariableRateShadingSupported() const
{
    return m_is_variable_rate_shading_supported;
}

bool VKDevice::IsMeshShadingSupported() const
{
    return m_is_mesh_shading_supported;
}

bool VKDevice::IsDrawIndirectCountSupported() const
{
    return m_draw_indirect_count_supported;
}

bool VKDevice::IsGeometryShaderSupported() const
{
    return m_geometry_shader_supported;
}

uint32_t VKDevice::GetShadingRateImageTileSize() const
{
    return m_shading_rate_image_tile_size;
}

MemoryBudget VKDevice::GetMemoryBudget() const
{
    vk::PhysicalDeviceMemoryBudgetPropertiesEXT memory_budget = {};
    vk::PhysicalDeviceMemoryProperties2 mem_properties = {};
    mem_properties.pNext = &memory_budget;
    m_physical_device.getMemoryProperties2(&mem_properties);
    MemoryBudget res = {};
    for (size_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i) {
        res.budget += memory_budget.heapBudget[i];
        res.usage += memory_budget.heapUsage[i];
    }
    return res;
}

uint32_t VKDevice::GetShaderGroupHandleSize() const
{
    return m_shader_group_handle_size;
}

uint32_t VKDevice::GetShaderRecordAlignment() const
{
    return m_shader_record_alignment;
}

uint32_t VKDevice::GetShaderTableAlignment() const
{
    return m_shader_table_alignment;
}

RaytracingASPrebuildInfo VKDevice::GetBLASPrebuildInfo(const std::vector<RaytracingGeometryDesc>& descs,
                                                       BuildAccelerationStructureFlags flags) const
{
    std::vector<vk::AccelerationStructureGeometryKHR> geometry_descs;
    std::vector<uint32_t> max_primitive_counts;
    for (const auto& desc : descs) {
        geometry_descs.emplace_back(FillRaytracingGeometryTriangles(desc.vertex, desc.index, desc.flags));
        if (desc.index.res) {
            max_primitive_counts.emplace_back(desc.index.count / 3);
        } else {
            max_primitive_counts.emplace_back(desc.vertex.count / 3);
        }
    }
    vk::AccelerationStructureBuildGeometryInfoKHR acceleration_structure_info = {};
    acceleration_structure_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    acceleration_structure_info.geometryCount = geometry_descs.size();
    acceleration_structure_info.pGeometries = geometry_descs.data();
    acceleration_structure_info.flags = Convert(flags);
    return GetAccelerationStructurePrebuildInfo(acceleration_structure_info, max_primitive_counts);
}

RaytracingASPrebuildInfo VKDevice::GetTLASPrebuildInfo(uint32_t instance_count,
                                                       BuildAccelerationStructureFlags flags) const
{
    vk::AccelerationStructureGeometryKHR geometry_info{};
    geometry_info.geometryType = vk::GeometryTypeKHR::eInstances;
    geometry_info.geometry.setInstances({});

    vk::AccelerationStructureBuildGeometryInfoKHR acceleration_structure_info = {};
    acceleration_structure_info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    acceleration_structure_info.pGeometries = &geometry_info;
    acceleration_structure_info.geometryCount = 1;
    acceleration_structure_info.flags = Convert(flags);
    return GetAccelerationStructurePrebuildInfo(acceleration_structure_info, { instance_count });
}

ShaderBlobType VKDevice::GetSupportedShaderBlobType() const
{
    return ShaderBlobType::kSPIRV;
}

VKAdapter& VKDevice::GetAdapter()
{
    return m_adapter;
}

vk::Device VKDevice::GetDevice()
{
    return m_device.get();
}

CommandListType VKDevice::GetAvailableCommandListType(CommandListType type)
{
    if (m_queues_info.contains(type)) {
        return type;
    }
    return CommandListType::kGraphics;
}

vk::CommandPool VKDevice::GetCmdPool(CommandListType type)
{
    return m_cmd_pools.at(GetAvailableCommandListType(type)).get();
}

vk::ImageAspectFlags VKDevice::GetAspectFlags(vk::Format format) const
{
    switch (format) {
    case vk::Format::eD32SfloatS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD16UnormS8Uint:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    case vk::Format::eD16Unorm:
    case vk::Format::eD32Sfloat:
    case vk::Format::eX8D24UnormPack32:
        return vk::ImageAspectFlagBits::eDepth;
    case vk::Format::eS8Uint:
        return vk::ImageAspectFlagBits::eStencil;
    default:
        return vk::ImageAspectFlagBits::eColor;
    }
}

uint32_t VKDevice::FindMemoryType(uint32_t type_filter, vk::MemoryPropertyFlags properties)
{
    vk::PhysicalDeviceMemoryProperties memProperties;
    m_physical_device.getMemoryProperties(&memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

VKGPUBindlessDescriptorPoolTyped& VKDevice::GetGPUBindlessDescriptorPool(vk::DescriptorType type)
{
    auto it = m_gpu_bindless_descriptor_pool.find(type);
    if (it == m_gpu_bindless_descriptor_pool.end()) {
        it = m_gpu_bindless_descriptor_pool
                 .emplace(std::piecewise_construct, std::forward_as_tuple(type), std::forward_as_tuple(*this, type))
                 .first;
    }
    return it->second;
}

VKGPUDescriptorPool& VKDevice::GetGPUDescriptorPool()
{
    return m_gpu_descriptor_pool;
}

uint32_t VKDevice::GetMaxDescriptorSetBindings(vk::DescriptorType type) const
{
    switch (type) {
    case vk::DescriptorType::eSampler:
        return m_device_properties.limits.maxPerStageDescriptorSamplers;
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eUniformTexelBuffer:
        return m_device_properties.limits.maxPerStageDescriptorSampledImages;
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
        return m_device_properties.limits.maxPerStageDescriptorUniformBuffers;
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eStorageBufferDynamic:
        return m_device_properties.limits.maxPerStageDescriptorStorageBuffers;
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eStorageTexelBuffer:
        return m_device_properties.limits.maxPerStageDescriptorStorageImages;
    default:
        assert(false);
        return 0;
    }
}

bool VKDevice::IsAtLeastVulkan12() const
{
    return m_is_at_least_vulkan12;
}

bool VKDevice::HasBufferDeviceAddress() const
{
    return m_has_buffer_device_address;
}
