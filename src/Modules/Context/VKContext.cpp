#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XCB_KHR
#endif
#include <GLFW/glfw3.h>
#include <vulkan/vk_sdk_platform.h>
#include "VKContext.h"
#include <Resource/VKResource.h>
#include <Program/VKProgramApi.h>
#include <Geometry/IABuffer.h>
#include <Texture/TextureLoader.h>
#include <glm/glm.hpp>
#include <gli/gli.hpp>
#include <Utilities/VKUtility.h>
#include <Utilities/State.h>

PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructure;
PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructure;
PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemory;
PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandle;
PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirements;
PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructure;
PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelines;
PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandles;
PFN_vkCmdTraceRaysNV vkCmdTraceRays;

VkIndexType GetVkIndexType(gli::format Format)
{
    VkFormat format = static_cast<VkFormat>(Format);
    switch (format)
    {
    case VK_FORMAT_R16_UINT:
        return VK_INDEX_TYPE_UINT16;
    case VK_FORMAT_R32_UINT:
        return VK_INDEX_TYPE_UINT32;
    default:
        assert(false);
        return {};
    }
}

class DebugReportListener
{
public:
    DebugReportListener(VkInstance instance)
        : m_vkCreateDebugReportCallbackEXT(reinterpret_cast<decltype(m_vkCreateDebugReportCallbackEXT)>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT")))
    {
        VkDebugReportCallbackCreateInfoEXT callback_create_info = {};
        callback_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        callback_create_info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                     VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                                     VK_DEBUG_REPORT_ERROR_BIT_EXT |
                                     VK_DEBUG_REPORT_DEBUG_BIT_EXT;
        callback_create_info.pfnCallback = &MyDebugReportCallback;

        VkDebugReportCallbackEXT callback;
        ASSERT_SUCCEEDED(m_vkCreateDebugReportCallbackEXT(instance, &callback_create_info, nullptr, &callback));
    }

private:
    static bool SkipIt(VkDebugReportObjectTypeEXT object_type, const std::string& message)
    {
        switch (object_type)
        {
        case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT:
       /* case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT:
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT:
        case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT:*/
            return true;
        default:
            return false;
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL MyDebugReportCallback(
        VkDebugReportFlagsEXT       flags,
        VkDebugReportObjectTypeEXT  objectType,
        uint64_t                    object,
        size_t                      location,
        int32_t                     messageCode,
        const char*                 pLayerPrefix,
        const char*                 pMessage,
        void*                       pUserData)
    {
        if (SkipIt(objectType, pMessage))
            return VK_FALSE;

#if defined(_DEBUG)
        static constexpr size_t errors_limit = 1000;
#else
        static constexpr size_t errors_limit = 10;
#endif
        static size_t cnt = 0;
        if (++cnt <= errors_limit)
            printf("%s\n", pMessage);
        if (cnt == errors_limit)
            printf("too much error messages");
        return VK_FALSE;
    }

    decltype(&vkCreateDebugReportCallbackEXT) m_vkCreateDebugReportCallbackEXT;
};

void VKContext::CreateInstance()
{
    uint32_t layer_count = 0;
    ASSERT_SUCCEEDED(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    ASSERT_SUCCEEDED(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));

    std::set<std::string> req_layers = {
#if defined(_DEBUG)
        "VK_LAYER_LUNARG_standard_validation"
#endif
    };
    std::vector<const char*> found_layers;
    for (const auto& layer : layers)
    {
        if (req_layers.count(layer.layerName))
            found_layers.push_back(layer.layerName);
    }
    
    uint32_t extension_count = 0;
    ASSERT_SUCCEEDED(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));
    std::vector<VkExtensionProperties> extensions(extension_count);
    ASSERT_SUCCEEDED(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()));

    std::set<std::string> req_extension = {
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
    #if defined(VK_USE_PLATFORM_WIN32_KHR)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    #elif defined(VK_USE_PLATFORM_XLIB_KHR)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    #elif defined(VK_USE_PLATFORM_XCB_KHR)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    #endif
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    std::vector<const char*> found_extension;
    for (const auto& extension : extensions)
    {
        if (req_extension.count(extension.extensionName))
            found_extension.push_back(extension.extensionName);
    }

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = found_layers.size();
    create_info.ppEnabledLayerNames = found_layers.data();
    create_info.enabledExtensionCount = found_extension.size();
    create_info.ppEnabledExtensionNames = found_extension.data();

    ASSERT_SUCCEEDED(vkCreateInstance(&create_info, nullptr, &m_instance));

#if defined(_DEBUG)
    DebugReportListener{ m_instance };
#endif
}

void VKContext::SelectPhysicalDevice()
{
    uint32_t device_count = 0;
    ASSERT_SUCCEEDED(vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    ASSERT_SUCCEEDED(vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data()));

    uint32_t gpu_index = 0;
    for (const auto& device : devices)
    {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);

        if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            if (CurState::Instance().required_gpu_index != -1 && gpu_index++ != CurState::Instance().required_gpu_index)
                continue;
            m_physical_device = device;
            CurState::Instance().gpu_name = device_properties.deviceName;
            break;
        }
    }
}

void VKContext::SelectQueueFamilyIndex()
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, queue_families.data());

    m_queue_family_index = -1;
    for (size_t i = 0; i < queue_families.size(); ++i)
    {
        const auto& queue = queue_families[i];
        if (queue.queueCount > 0 && queue.queueFlags & VK_QUEUE_GRAPHICS_BIT && queue.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            m_queue_family_index = static_cast<uint32_t>(i);
            break;
        }
    }
    ASSERT(m_queue_family_index != -1);
}

void VKContext::CreateDevice()
{
    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = m_queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features = {};
    device_features.textureCompressionBC = true;
    device_features.vertexPipelineStoresAndAtomics = true;
    device_features.samplerAnisotropy = true;
    device_features.fragmentStoresAndAtomics = true;
    device_features.sampleRateShading = true;
    device_features.geometryShader = true;
    device_features.imageCubeArray = true;

    uint32_t extension_count = 0;
    ASSERT_SUCCEEDED(vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &extension_count, nullptr));
    std::vector<VkExtensionProperties> extensions(extension_count);
    ASSERT_SUCCEEDED(vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &extension_count, extensions.data()));
    std::set<std::string> req_extension = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
        VK_NV_RAY_TRACING_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
    };
    std::vector<const char*> found_extension;
    for (const auto& extension : extensions)
    {
        if (req_extension.count(extension.extensionName))
            found_extension.push_back(extension.extensionName);
    }

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.pEnabledFeatures = &device_features;
    device_create_info.enabledExtensionCount = found_extension.size();
    device_create_info.ppEnabledExtensionNames = found_extension.data();

    ASSERT_SUCCEEDED(vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device));


    // Get VK_NV_ray_tracing related function pointers
    vkCreateAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureNV>(vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureNV"));
    vkDestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureNV>(vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureNV"));
    vkBindAccelerationStructureMemory = reinterpret_cast<PFN_vkBindAccelerationStructureMemoryNV>(vkGetDeviceProcAddr(m_device, "vkBindAccelerationStructureMemoryNV"));
    vkGetAccelerationStructureHandle = reinterpret_cast<PFN_vkGetAccelerationStructureHandleNV>(vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureHandleNV"));
    vkGetAccelerationStructureMemoryRequirements = reinterpret_cast<PFN_vkGetAccelerationStructureMemoryRequirementsNV>(vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureMemoryRequirementsNV"));
    vkCmdBuildAccelerationStructure = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructureNV"));
    vkCreateRayTracingPipelines = reinterpret_cast<PFN_vkCreateRayTracingPipelinesNV>(vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesNV"));
    vkGetRayTracingShaderGroupHandles = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesNV>(vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesNV"));
    vkCmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysNV"));
}

void VKContext::CreateSwapchain(int width, int height)
{
    uint32_t format_count = 0;
    ASSERT_SUCCEEDED(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, nullptr));
    ASSERT(format_count >= 1);
    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    ASSERT_SUCCEEDED(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, surface_formats.data()));

    if (surface_formats.front().format != VK_FORMAT_UNDEFINED)
        m_swapchain_color_format = surface_formats.front().format;

    VkColorSpaceKHR color_space = surface_formats.front().colorSpace;

    VkSurfaceCapabilitiesKHR surface_capabilities = {};
    ASSERT_SUCCEEDED(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities));

    ASSERT(surface_capabilities.currentExtent.width == width);
    ASSERT(surface_capabilities.currentExtent.height == height);

    VkBool32 is_supported_surface = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, m_queue_family_index, m_surface, &is_supported_surface);
    ASSERT(is_supported_surface);

    VkSwapchainCreateInfoKHR swap_chain_create_info = {};
    swap_chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_chain_create_info.surface = m_surface;
    swap_chain_create_info.minImageCount = FrameCount;
    swap_chain_create_info.imageFormat = m_swapchain_color_format;
    swap_chain_create_info.imageColorSpace = color_space;
    swap_chain_create_info.imageExtent = surface_capabilities.currentExtent;
    swap_chain_create_info.imageArrayLayers = 1;
    swap_chain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swap_chain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_chain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swap_chain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (CurState::Instance().vsync)
        swap_chain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    else
        swap_chain_create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    swap_chain_create_info.clipped = true;

    ASSERT_SUCCEEDED(vkCreateSwapchainKHR(m_device, &swap_chain_create_info, nullptr, &m_swapchain));
}

VKContext::VKContext(GLFWwindow* window)
    : Context(window)
{
    CreateInstance();
    SelectPhysicalDevice();
    SelectQueueFamilyIndex();
    CreateDevice();
    vkGetDeviceQueue(m_device, m_queue_family_index, 0, &m_queue);
    ASSERT_SUCCEEDED(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
    CreateSwapchain(m_width, m_height);

    uint32_t frame_buffer_count = 0;
    ASSERT_SUCCEEDED(vkGetSwapchainImagesKHR(m_device, m_swapchain, &frame_buffer_count, nullptr));
    m_images.resize(frame_buffer_count);
    ASSERT_SUCCEEDED(vkGetSwapchainImagesKHR(m_device, m_swapchain, &frame_buffer_count, m_images.data()));

    VkCommandPoolCreateInfo cmd_pool_create_info = {};
    cmd_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmd_pool_create_info.queueFamilyIndex = m_queue_family_index;
    ASSERT_SUCCEEDED(vkCreateCommandPool(m_device, &cmd_pool_create_info, nullptr, &m_cmd_pool));

    m_cmd_bufs.resize(frame_buffer_count);
    VkCommandBufferAllocateInfo cmd_buf_alloc_info = {};
    cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_alloc_info.commandPool = m_cmd_pool;
    cmd_buf_alloc_info.commandBufferCount = m_cmd_bufs.size();
    cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ASSERT_SUCCEEDED(vkAllocateCommandBuffers(m_device, &cmd_buf_alloc_info, m_cmd_bufs.data()));

    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    ASSERT_SUCCEEDED(vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &m_image_available_semaphore));
    ASSERT_SUCCEEDED(vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &m_rendering_finished_semaphore));

    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ASSERT_SUCCEEDED(vkCreateFence(m_device, &fence_create_info, nullptr, &m_fence));

    ASSERT(frame_buffer_count == FrameCount);
    for (size_t i = 0; i < FrameCount; ++i)
    {
        descriptor_pool[i].reset(new VKDescriptorPool(*this));
    }

    OpenCommandBuffer();

    for (size_t i = 0; i < FrameCount; ++i)
    {
        VKResource::Ptr res = std::make_shared<VKResource>();
        res->image.res = m_images[i];
        res->image.format = m_swapchain_color_format;
        res->image.size = { 1u * m_width, 1u * m_height };
        res->res_type = VKResource::Type::kImage;
        m_back_buffers[i] = res;
    }
}

std::unique_ptr<ProgramApi> VKContext::CreateProgram()
{
    auto res = std::make_unique<VKProgramApi>(*this);
    m_created_program.push_back(*res.get());
    return res;
}

VkFormat VKContext::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

Resource::Ptr VKContext::CreateTexture(uint32_t bind_flag, gli::format format, uint32_t msaa_count, int width, int height, int depth, int mip_levels)
{
    VKResource::Ptr res = std::make_shared<VKResource>();
    res->res_type = VKResource::Type::kImage;

    VkFormat vk_format = static_cast<VkFormat>(format);
    if (vk_format == VK_FORMAT_D24_UNORM_S8_UINT)
        vk_format = VK_FORMAT_D32_SFLOAT_S8_UINT;

    auto createImage = [this, msaa_count](int width, int height, int depth, int mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
        VkImage& image, VkDeviceMemory& imageMemory, uint32_t& size)
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mip_levels;
        imageInfo.arrayLayers = depth;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = (VkSampleCountFlagBits)msaa_count;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (depth % 6 == 0)
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(m_device, image, imageMemory, 0);

        size = allocInfo.allocationSize;
    };
    
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (bind_flag & BindFlag::kDsv)
        usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (bind_flag & BindFlag::kSrv)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (bind_flag & BindFlag::kRtv)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (bind_flag & BindFlag::kUav)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    uint32_t tmp = 0;
    createImage(
        width,
        height,
        depth,
        mip_levels,
        vk_format,
        VK_IMAGE_TILING_OPTIMAL,
        usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        res->image.res,
        res->image.memory,
        tmp
    );

    res->image.size.height = height;
    res->image.size.width = width;
    res->image.format = vk_format;
    res->image.level_count = mip_levels;
    res->image.msaa_count = msaa_count;
    res->image.array_layers = depth;
   
    return res;
}

uint32_t VKContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

Resource::Ptr VKContext::CreateBuffer(uint32_t bind_flag, uint32_t buffer_size, uint32_t stride)
{
    if (buffer_size == 0)
        return VKResource::Ptr();

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = buffer_size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (bind_flag & BindFlag::kVbv)
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    else if (bind_flag & BindFlag::kIbv)
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    else if (bind_flag & BindFlag::kCbv)
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    else if (bind_flag & BindFlag::kSrv)
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    else
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VKResource::Ptr res = std::make_shared<VKResource>();
    res->res_type = VKResource::Type::kBuffer;

    vkCreateBuffer(m_device, &bufferInfo, nullptr, &res->buffer.res);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, res->buffer.res, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);


    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &res->buffer.memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(m_device, res->buffer.res, res->buffer.memory, 0);
    res->buffer.size = buffer_size;

    return res;
}

Resource::Ptr VKContext::CreateSampler(const SamplerDesc & desc)
{
    VKResource::Ptr res = std::make_shared<VKResource>();

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
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

    switch (desc.mode)
    {
    case SamplerTextureAddressMode::kWrap:
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    case SamplerTextureAddressMode::kClamp:
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    }

    switch (desc.func)
    {
    case SamplerComparisonFunc::kNever:
        samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        break;
    case SamplerComparisonFunc::kAlways:
        samplerInfo.compareEnable = true;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        break;
    case SamplerComparisonFunc::kLess:
        samplerInfo.compareEnable = true;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS;
        break;
    }

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &res->sampler.res) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }

    res->res_type = VKResource::Type::kSampler;
    return res;
}

VkImageAspectFlags VKContext::GetAspectFlags(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

void VKContext::TransitionImageLayout(VKResource::Image& image, VkImageLayout newLayout, const ViewDesc& view_desc)
{
    VkImageSubresourceRange range = {};
    range.aspectMask = GetAspectFlags(image.format);
    range.baseMipLevel = view_desc.level;
    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        range.levelCount = 1;
    else if (view_desc.count == -1)
        range.levelCount = image.level_count - view_desc.level;
    else
        range.levelCount = view_desc.count;
    range.baseArrayLayer = 0;
    range.layerCount = image.array_layers;

    std::vector<VkImageMemoryBarrier> image_memory_barriers;

    for (uint32_t i = 0; i < range.levelCount; ++i)
    {
        for (uint32_t j = 0; j < range.layerCount; ++j)
        {
            VkImageSubresourceRange barrier_range = range;
            barrier_range.baseMipLevel = range.baseMipLevel + i;
            barrier_range.levelCount = 1;
            barrier_range.baseArrayLayer = range.baseArrayLayer + j;
            barrier_range.layerCount = 1;

            if (image.layout[barrier_range] == newLayout)
                continue;

            image_memory_barriers.emplace_back();
            VkImageMemoryBarrier& imageMemoryBarrier = image_memory_barriers.back();
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.oldLayout = image.layout[barrier_range];

            imageMemoryBarrier.newLayout = newLayout;
            imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.image = image.res;
            imageMemoryBarrier.subresourceRange = barrier_range;

            // Source layouts (old)
            // Source access mask controls actions that have to be finished on the old layout
            // before it will be transitioned to the new layout
            switch (image.layout[barrier_range])
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // Image layout is undefined (or does not matter)
                // Only valid as initial layout
                // No flags required, listed only for completeness
                imageMemoryBarrier.srcAccessMask = 0;
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                // Image is preinitialized
                // Only valid as initial layout for linear images, preserves memory contents
                // Make sure host writes have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image is a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image is a depth/stencil attachment
                // Make sure any writes to the depth/stencil buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image is a transfer source 
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image is a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image is read by a shader
                // Make sure any shader reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            // Target layouts (new)
            // Destination access mask controls the dependency for the new image layout
            switch (newLayout)
            {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image will be used as a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image will be used as a transfer source
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image will be used as a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image layout will be used as a depth/stencil attachment
                // Make sure any writes to depth/stencil buffer have been finished
                imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image will be read in a shader (sampler, input attachment)
                // Make sure any writes to the image have been finished
                if (imageMemoryBarrier.srcAccessMask == 0)
                {
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                }
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            image.layout[barrier_range] = newLayout;
        }
    }

    vkCmdPipelineBarrier(
        m_cmd_bufs[m_frame_index],
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        image_memory_barriers.size(), image_memory_barriers.data());
};

void VKContext::UpdateSubresource(const Resource::Ptr & ires, uint32_t DstSubresource, const void * pSrcData, uint32_t SrcRowPitch, uint32_t SrcDepthPitch)
{
    if (!ires)
        return;
    auto res = std::static_pointer_cast<VKResource>(ires);

    if (res->res_type == VKResource::Type::kBuffer)
    {
        void* data;
        vkMapMemory(m_device, res->buffer.memory, 0, res->buffer.size, 0, &data);
        memcpy(data, pSrcData, (size_t)res->buffer.size);
        vkUnmapMemory(m_device, res->buffer.memory);
    }
    else if (res->res_type == VKResource::Type::kImage)
    {
        auto staging = res->GetUploadResource(DstSubresource);
        if (!staging || staging->res_type == VKResource::Type::kUnknown)
            staging = std::static_pointer_cast<VKResource>(CreateBuffer(0, SrcDepthPitch, 0));
        UpdateSubresource(staging, 0, pSrcData, SrcRowPitch, SrcDepthPitch);

        // Setup buffer copy regions for each mip level
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        uint32_t offset = 0;

        VkBufferImageCopy bufferCopyRegion = {};
        bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.mipLevel = DstSubresource;
        bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = std::max(1u, static_cast<uint32_t>(res->image.size.width >> DstSubresource));
        bufferCopyRegion.imageExtent.height = std::max(1u, static_cast<uint32_t>(res->image.size.height >> DstSubresource));
        bufferCopyRegion.imageExtent.depth = 1;

        bufferCopyRegions.push_back(bufferCopyRegion);

        // The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
        VkImageSubresourceRange subresourceRange = {};
        // Image only contains color data
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // Start at first mip level
        subresourceRange.baseMipLevel = DstSubresource;
        // We will transition on all mip levels
        subresourceRange.levelCount = 1;
        // The 2D texture only has one layer
        subresourceRange.layerCount = 1;

        // Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.image = res->image.res;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        imageMemoryBarrier.srcAccessMask = 0;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
        // Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
        // Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
        vkCmdPipelineBarrier(
            m_cmd_bufs[m_frame_index],
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageMemoryBarrier);

        // Copy mip levels from staging buffer
        vkCmdCopyBufferToImage(
            m_cmd_bufs[m_frame_index],
            staging->buffer.res,
            res->image.res,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(bufferCopyRegions.size()),
            bufferCopyRegions.data());

        // Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
        // Source pipeline stage stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
        // Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        vkCmdPipelineBarrier(
            m_cmd_bufs[m_frame_index],
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageMemoryBarrier);

        // Store current layout for later reuse
        res->image.layout[subresourceRange] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

void VKContext::SetViewport(float width, float height)
{
    VkViewport viewport{};
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1.0;
    vkCmdSetViewport(m_cmd_bufs[m_frame_index], 0, 1, &viewport);

    SetScissorRect(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
}

void VKContext::SetScissorRect(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    VkRect2D rect2D{};
    rect2D.extent.width = right;
    rect2D.extent.height = bottom;
    rect2D.offset.x = left;
    rect2D.offset.y = top;
    vkCmdSetScissor(m_cmd_bufs[m_frame_index], 0, 1, &rect2D);
}

Resource::Ptr VKContext::CreateBottomLevelAS(const BufferDesc& vertex)
{
    return CreateBottomLevelAS(vertex, {});
}

Resource::Ptr VKContext::CreateBottomLevelAS(const BufferDesc& vertex, const BufferDesc& index)
{
    AccelerationStructure bottomLevelAS;

    auto vertex_res = std::static_pointer_cast<VKResource>(vertex.res);
    auto index_res = std::static_pointer_cast<VKResource>(index.res);

    auto vertex_stride = gli::detail::bits_per_pixel(vertex.format) / 8;

    VkGeometryNV& geometry = bottomLevelAS.geometry;
    geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
    geometry.geometry.triangles.vertexData = vertex_res->buffer.res;
    geometry.geometry.triangles.vertexOffset = vertex.offset;
    geometry.geometry.triangles.vertexCount = vertex.count;
    geometry.geometry.triangles.vertexStride = vertex_stride;
    geometry.geometry.triangles.vertexFormat = static_cast<VkFormat>(vertex.format);
    if (index_res)
    {
        geometry.geometry.triangles.indexData = index_res->buffer.res;
        geometry.geometry.triangles.indexOffset = index.offset;
        geometry.geometry.triangles.indexCount = index.count;
        geometry.geometry.triangles.indexType = GetVkIndexType(index.format);
    }
    else
    {
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_NV;
    }
    geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
    geometry.geometry.triangles.transformOffset = 0;
    geometry.geometry.aabbs = {};
    geometry.geometry.aabbs.sType = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

    VkAccelerationStructureInfoNV accelerationStructureInfo{};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    accelerationStructureInfo.instanceCount = 0;
    accelerationStructureInfo.geometryCount = 1;
    accelerationStructureInfo.pGeometries = &geometry;

    VkAccelerationStructureCreateInfoNV accelerationStructureCI{};
    accelerationStructureCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
    accelerationStructureCI.info = accelerationStructureInfo;
    ASSERT_SUCCEEDED(vkCreateAccelerationStructure(m_device, &accelerationStructureCI, nullptr, &bottomLevelAS.accelerationStructure));

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
    memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = bottomLevelAS.accelerationStructure;

    VkMemoryRequirements2 memoryRequirements2{};
    vkGetAccelerationStructureMemoryRequirements(m_device, &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_SUCCEEDED(vkAllocateMemory(m_device, &memoryAllocateInfo, nullptr, &bottomLevelAS.memory));

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo{};
    accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
    accelerationStructureMemoryInfo.accelerationStructure = bottomLevelAS.accelerationStructure;
    accelerationStructureMemoryInfo.memory = bottomLevelAS.memory;
    ASSERT_SUCCEEDED(vkBindAccelerationStructureMemory(m_device, 1, &accelerationStructureMemoryInfo));

    ASSERT_SUCCEEDED(vkGetAccelerationStructureHandle(m_device, bottomLevelAS.accelerationStructure, sizeof(uint64_t), &bottomLevelAS.handle));

    VKResource::Ptr res = std::make_shared<VKResource>();
    res->res_type = VKResource::Type::kBottomLevelAS;
    res->bottom_as = bottomLevelAS;

    return res;
}

Resource::Ptr VKContext::CreateTopLevelAS(const std::vector<std::pair<Resource::Ptr, glm::mat4>>& geometry)
{
    AccelerationStructure topLevelAS;

    VkAccelerationStructureInfoNV accelerationStructureInfo{};
    accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    accelerationStructureInfo.instanceCount = geometry.size();
    accelerationStructureInfo.geometryCount = 0;

    VkAccelerationStructureCreateInfoNV accelerationStructureCI{};
    accelerationStructureCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
    accelerationStructureCI.info = accelerationStructureInfo;
    ASSERT_SUCCEEDED(vkCreateAccelerationStructure(m_device, &accelerationStructureCI, nullptr, &topLevelAS.accelerationStructure));

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
    memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = topLevelAS.accelerationStructure;

    VkMemoryRequirements2 memoryRequirements2{};
    vkGetAccelerationStructureMemoryRequirements(m_device, &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_SUCCEEDED(vkAllocateMemory(m_device, &memoryAllocateInfo, nullptr, &topLevelAS.memory));

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo{};
    accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
    accelerationStructureMemoryInfo.accelerationStructure = topLevelAS.accelerationStructure;
    accelerationStructureMemoryInfo.memory = topLevelAS.memory;
    ASSERT_SUCCEEDED(vkBindAccelerationStructureMemory(m_device, 1, &accelerationStructureMemoryInfo));

    ASSERT_SUCCEEDED(vkGetAccelerationStructureHandle(m_device, topLevelAS.accelerationStructure, sizeof(uint64_t), &topLevelAS.handle));




    /*
            Build the acceleration structure
        */

        // Acceleration structure build requires some scratch space to store temporary information
    memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkDeviceSize maximumBlasSize = 0;
    for (auto& mesh : geometry)
    {
        auto res = std::static_pointer_cast<VKResource>(mesh.first);

        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
        memoryRequirementsInfo.accelerationStructure = res->bottom_as.accelerationStructure;

        VkMemoryRequirements2 memReqBLAS;
        vkGetAccelerationStructureMemoryRequirements(m_device, &memoryRequirementsInfo, &memReqBLAS);

        maximumBlasSize = std::max(maximumBlasSize, memReqBLAS.memoryRequirements.size);
    }

    VkMemoryRequirements2 memReqTopLevelAS;
    memoryRequirementsInfo.accelerationStructure = topLevelAS.accelerationStructure;
    vkGetAccelerationStructureMemoryRequirements(m_device, &memoryRequirementsInfo, &memReqTopLevelAS);

    const VkDeviceSize scratchBufferSize = std::max(maximumBlasSize, memReqTopLevelAS.memoryRequirements.size);


    VkBuffer scratchBuffer;
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = scratchBufferSize;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        vkCreateBuffer(m_device, &bufferInfo, nullptr, &scratchBuffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, scratchBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory memory;
        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate vertex buffer memory!");
        }

        vkBindBufferMemory(m_device, scratchBuffer, memory, 0);
    }

    /*
        Build bottom level acceleration structure
    */

    for (auto& mesh : geometry)
    {
        auto res = std::static_pointer_cast<VKResource>(mesh.first);
        
        VkAccelerationStructureInfoNV buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
        buildInfo.instanceCount = 0;
        buildInfo.geometryCount = 1;

        buildInfo.pGeometries = &res->bottom_as.geometry;

        vkCmdBuildAccelerationStructure(
            m_cmd_bufs[m_frame_index],
            &buildInfo,
            VK_NULL_HANDLE,
            0,
            VK_FALSE,
            res->bottom_as.accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer,
            0);
    }

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.pNext = nullptr;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    vkCmdPipelineBarrier(m_cmd_bufs[m_frame_index], VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

    /*
        Build top-level acceleration structure
    */
    VkAccelerationStructureInfoNV buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.pGeometries = 0;
    buildInfo.geometryCount = 0;
    buildInfo.instanceCount = 1;

    std::vector<GeometryInstance> instances;
    for (auto& mesh : geometry)
    {
        auto res = std::static_pointer_cast<VKResource>(mesh.first);

        instances.emplace_back();
        GeometryInstance& instance = instances.back();
        auto t = mesh.second;
        memcpy(&instance.transform, &t, sizeof(instance.transform));
        instance.instanceId = static_cast<uint32_t>(instances.size() - 1);
        instance.mask = 0xff;
        instance.instanceOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
        instance.accelerationStructureHandle = res->bottom_as.handle;
    }

    VkBuffer geometryInstance;
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = instances.size() * sizeof(instances.back());
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        vkCreateBuffer(m_device, &bufferInfo, nullptr, &geometryInstance);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, geometryInstance, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory memory;
        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate vertex buffer memory!");
        }

        vkBindBufferMemory(m_device, geometryInstance, memory, 0);

        void* data;
        vkMapMemory(m_device, memory, 0, bufferInfo.size, 0, &data);
        memcpy(data, instances.data(), (size_t)bufferInfo.size);
        vkUnmapMemory(m_device, memory);
    }

    vkCmdBuildAccelerationStructure(
        m_cmd_bufs[m_frame_index],
        &buildInfo,
        geometryInstance,
        0,
        VK_FALSE,
        topLevelAS.accelerationStructure,
        VK_NULL_HANDLE,
        scratchBuffer,
        0);

    vkCmdPipelineBarrier(m_cmd_bufs[m_frame_index], VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

    VKResource::Ptr res = std::make_shared<VKResource>();
    res->res_type = VKResource::Type::kTopLevelAS;
    res->top_as = topLevelAS;

    return res;
}

void VKContext::UseProgram(ProgramApi& program)
{
    auto& program_api = static_cast<VKProgramApi&>(program);
    if (m_current_program != &program_api)
    {
        if (m_is_open_render_pass)
        {
            vkCmdEndRenderPass(m_cmd_bufs[GetFrameIndex()]);
            m_is_open_render_pass = false;
        }
    }
    m_current_program = &program_api;
    m_current_program->UseProgram();
}

void VKContext::IASetIndexBuffer(Resource::Ptr ires, gli::format Format)
{
    VKResource::Ptr res = std::static_pointer_cast<VKResource>(ires);
    VkIndexType index_type = GetVkIndexType(Format);
    vkCmdBindIndexBuffer(m_cmd_bufs[m_frame_index], res->buffer.res, 0, index_type);
}

void VKContext::IASetVertexBuffer(uint32_t slot, Resource::Ptr ires)
{
    VKResource::Ptr res = std::static_pointer_cast<VKResource>(ires);
    VkBuffer vertexBuffers[] = { res->buffer.res };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(m_cmd_bufs[m_frame_index], slot, 1, vertexBuffers, offsets);
}

void VKContext::BeginEvent(const std::string& name)
{
    static decltype(&vkCmdBeginDebugUtilsLabelEXT) vkCmdBeginDebugUtilsLabelEXT_fn = decltype(&vkCmdBeginDebugUtilsLabelEXT)(vkGetDeviceProcAddr(m_device, "vkCmdBeginDebugUtilsLabelEXT"));
    if (!vkCmdBeginDebugUtilsLabelEXT_fn)
        return;
    VkDebugUtilsLabelEXT label = {};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name.c_str();
    vkCmdBeginDebugUtilsLabelEXT_fn(m_cmd_bufs[m_frame_index], &label);
}

void VKContext::EndEvent()
{
    static decltype(&vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT_fn = decltype(&vkCmdEndDebugUtilsLabelEXT)(vkGetDeviceProcAddr(m_device, "vkCmdEndDebugUtilsLabelEXT"));
    if (!vkCmdEndDebugUtilsLabelEXT_fn)
        return;
    vkCmdEndDebugUtilsLabelEXT_fn(m_cmd_bufs[m_frame_index]);
}

void VKContext::DrawIndexed(uint32_t IndexCount, uint32_t StartIndexLocation, int32_t BaseVertexLocation)
{
    m_current_program->ApplyBindings();

    auto rp = m_current_program->GetRenderPass();
    auto fb = m_current_program->GetFramebuffer();
    if (rp != m_render_pass || fb != m_framebuffer)
    {
        if (m_is_open_render_pass)
            vkCmdEndRenderPass(m_cmd_bufs[GetFrameIndex()]);
        m_render_pass = rp;
        m_framebuffer = fb;
        m_current_program->RenderPassBegin();
        m_is_open_render_pass = true;
    }
    vkCmdDrawIndexed(m_cmd_bufs[m_frame_index], IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}

void VKContext::Dispatch(uint32_t ThreadGroupCountX, uint32_t ThreadGroupCountY, uint32_t ThreadGroupCountZ)
{
    m_current_program->ApplyBindings();
    vkCmdDispatch(m_cmd_bufs[m_frame_index], ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

void VKContext::DispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
    // Query the ray tracing properties of the current implementation, we will need them later on
    VkPhysicalDeviceRayTracingPropertiesNV rayTracingProperties{};
    rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps2.pNext = &rayTracingProperties;

    vkGetPhysicalDeviceProperties2(m_physical_device, &deviceProps2);

    VkDeviceSize bindingOffsetRayGenShader = rayTracingProperties.shaderGroupHandleSize * 0;
    VkDeviceSize bindingOffsetMissShader = rayTracingProperties.shaderGroupHandleSize * 1;
    VkDeviceSize bindingOffsetHitShader = rayTracingProperties.shaderGroupHandleSize * 2;
    VkDeviceSize bindingStride = rayTracingProperties.shaderGroupHandleSize;

    m_current_program->ApplyBindings();
    vkCmdTraceRays(m_cmd_bufs[m_frame_index],
        m_current_program->shaderBindingTable, bindingOffsetRayGenShader,
        m_current_program->shaderBindingTable, bindingOffsetMissShader, bindingStride,
        m_current_program->shaderBindingTable, bindingOffsetHitShader, bindingStride,
        VK_NULL_HANDLE, 0, 0,
        width, height, depth
    );
}

Resource::Ptr VKContext::GetBackBuffer()
{
    return m_back_buffers[m_frame_index];
}

void VKContext::CloseCommandBuffer()
{
    if (m_is_open_render_pass)
    {
        vkCmdEndRenderPass(m_cmd_bufs[m_frame_index]);
        m_is_open_render_pass = false;
    }

    TransitionImageLayout(m_back_buffers[m_frame_index]->image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, {});

    auto res = vkEndCommandBuffer(m_cmd_bufs[m_frame_index]);
}

void VKContext::Submit()
{
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmd_bufs[m_frame_index];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_image_available_semaphore;
    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_rendering_finished_semaphore;

    auto res = vkQueueSubmit(m_queue, 1, &submitInfo, m_fence);

    res = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
    {
        throw std::runtime_error("vkWaitForFences");
    }
    vkResetFences(m_device, 1, &m_fence);
}

void VKContext::SwapBuffers()
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_frame_index;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_rendering_finished_semaphore;

    auto res = vkQueuePresentKHR(m_queue, &presentInfo);
}

void VKContext::OpenCommandBuffer()
{
    vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_image_available_semaphore, nullptr, &m_frame_index);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    auto res = vkBeginCommandBuffer(m_cmd_bufs[m_frame_index], &beginInfo);
}

void VKContext::Present()
{
    CloseCommandBuffer();
    Submit();
    SwapBuffers();
    OpenCommandBuffer();

    descriptor_pool[m_frame_index]->OnFrameBegin();
    for (auto & x : m_created_program)
        x.get().OnPresent();
}

void VKContext::ResizeBackBuffer(int width, int height)
{
}

VKDescriptorPool& VKContext::GetDescriptorPool()
{
    return *descriptor_pool[m_frame_index];
}
