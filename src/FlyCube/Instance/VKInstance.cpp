#include "Instance/VKInstance.h"

#include "Adapter/VKAdapter.h"

#include <set>
#include <sstream>
#include <string>
#include <string_view>

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace {

bool SkipIt(vk::DebugReportFlagsEXT flags, vk::DebugReportObjectTypeEXT object_type, const std::string& message)
{
    if (object_type == vk::DebugReportObjectTypeEXT::eInstance && flags != vk::DebugReportFlagBitsEXT::eError) {
        return true;
    }
    static std::vector<std::string> muted_warnings = {
        "UNASSIGNED-CoreValidation-Shader-InconsistentSpirv",
        "VUID-vkCmdDrawIndexed-None-04007",
        "VUID-vkDestroyDevice-device-00378",
        "VUID-VkSubmitInfo-pWaitSemaphores-03243",
        "VUID-VkSubmitInfo-pSignalSemaphores-03244",
        "VUID-vkCmdPipelineBarrier-pDependencies-02285",
        "VUID-VkImageMemoryBarrier-oldLayout-01213",
        "VUID-vkCmdDrawIndexed-None-02721",
        "VUID-vkCmdDrawIndexed-None-02699",
        "VUID-vkCmdTraceRaysKHR-None-02699",
        "VUID-VkShaderModuleCreateInfo-pCode-04147",
    };
    for (auto& str : muted_warnings) {
        if (message.find(str) != std::string::npos) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugReportCallback(vk::DebugReportFlagsEXT flags,
                                                     vk::DebugReportObjectTypeEXT objectType,
                                                     uint64_t object,
                                                     size_t location,
                                                     int32_t messageCode,
                                                     const char* pLayerPrefix,
                                                     const char* pMessage,
                                                     void* pUserData)
{
    constexpr size_t error_limit = 1024;
    static size_t error_count = 0;
    if (error_count >= error_limit || SkipIt(flags, objectType, pMessage)) {
        return VK_FALSE;
    }
    if (error_count < error_limit) {
        std::stringstream buf;
        buf << pLayerPrefix << " " << std::to_string(static_cast<VkDebugReportFlagsEXT>(flags)) << " " << pMessage
            << std::endl;
#if defined(_WIN32)
        OutputDebugStringA(buf.str().c_str());
#elif defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_DEBUG, "Vulkan-Debug-Message: ", "%s", buf.str().c_str());
#else
        printf("%s\n", buf.str().c_str());
#endif
    }
    ++error_count;
    return VK_FALSE;
}

} // namespace

VKInstance::VKInstance()
{
#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        m_dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
#endif

    auto layers = vk::enumerateInstanceLayerProperties();

    std::set<std::string> req_layers;
#ifdef _WIN32
    static const bool debug_enabled = IsDebuggerPresent();
#else
    static const bool debug_enabled = true;
#endif
    if (debug_enabled) {
        req_layers.insert("VK_LAYER_KHRONOS_validation");
    }
    std::vector<const char*> found_layers;
    for (const auto& layer : layers) {
        if (req_layers.count(layer.layerName.data())) {
            found_layers.push_back(layer.layerName);
        }
    }

    auto extensions = vk::enumerateInstanceExtensionProperties();

    std::set<std::string_view> req_extension = {
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_METAL_EXT)
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#endif
    };
    std::vector<const char*> found_extension;
    vk::InstanceCreateInfo create_info;
    for (const auto& extension : extensions) {
        if (req_extension.count(extension.extensionName.data())) {
            found_extension.push_back(extension.extensionName);
        }

        if (std::string(extension.extensionName.data()) == VK_EXT_DEBUG_UTILS_EXTENSION_NAME) {
            m_debug_utils_supported = true;
        }
        if (std::string(extension.extensionName.data()) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
            create_info.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
        }
    }

    vk::ApplicationInfo app_info = {};
    app_info.apiVersion = std::max(VK_API_VERSION_1_1, vk::enumerateInstanceVersion());
    m_api_version = app_info.apiVersion;

    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = static_cast<uint32_t>(found_layers.size());
    create_info.ppEnabledLayerNames = found_layers.data();
    create_info.enabledExtensionCount = static_cast<uint32_t>(found_extension.size());
    create_info.ppEnabledExtensionNames = found_extension.data();

    m_instance = vk::createInstanceUnique(create_info);
#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());
#endif
    if (debug_enabled) {
        vk::DebugReportCallbackCreateInfoEXT callback_create_info = {};
        callback_create_info.flags = vk::DebugReportFlagBitsEXT::eWarning |
                                     vk::DebugReportFlagBitsEXT::ePerformanceWarning |
                                     vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eDebug;
        callback_create_info.pfnCallback = &DebugReportCallback;
        callback_create_info.pUserData = this;
        m_callback = m_instance->createDebugReportCallbackEXTUnique(callback_create_info);
    }
}

std::vector<std::shared_ptr<Adapter>> VKInstance::EnumerateAdapters()
{
    std::vector<std::shared_ptr<Adapter>> adapters;
    std::vector<std::shared_ptr<Adapter>> sortware_adapters;
    auto devices = m_instance->enumeratePhysicalDevices();
    for (const auto& device : devices) {
        vk::PhysicalDeviceProperties device_properties = device.getProperties();
        if (device_properties.apiVersion < VK_API_VERSION_1_1) {
            continue;
        }
        if (device_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ||
            device_properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
            adapters.emplace_back(std::make_shared<VKAdapter>(*this, device));
        } else if (device_properties.deviceType == vk::PhysicalDeviceType::eCpu) {
            sortware_adapters.emplace_back(std::make_shared<VKAdapter>(*this, device));
        }
    }
    if (adapters.empty()) {
        return sortware_adapters;
    }
    return adapters;
}

vk::Instance& VKInstance::GetInstance()
{
    return m_instance.get();
}

uint32_t VKInstance::GetApiVersion() const
{
    return m_api_version;
}

bool VKInstance::IsDebugUtilsSupported() const
{
    return m_debug_utils_supported;
}
