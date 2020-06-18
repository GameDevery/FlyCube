#include "Context/Context.h"
#include <Utilities/FormatHelper.h>

Context::Context(const Settings& settings, GLFWwindow* window)
    : m_window(window)
    , m_vsync(settings.vsync)
{
    m_instance = CreateInstance(settings.api_type);
    m_adapter = std::move(m_instance->EnumerateAdapters()[settings.required_gpu_index]);
    m_device = m_adapter->CreateDevice();

    glfwGetWindowSize(window, &m_width, &m_height);
    m_swapchain = m_device->CreateSwapchain(window, m_width, m_height, FrameCount, settings.vsync);
    m_fence = m_device->CreateFence(m_fence_value);
    for (uint32_t i = 0; i < FrameCount; ++i)
    {
        m_swapchain_command_lists.emplace_back(m_device->CreateCommandList());
        m_swapchain_fence_values.emplace_back(0);
    }
}

Context::~Context()
{
    WaitIdle();
}

std::shared_ptr<Resource> Context::GetBackBuffer(uint32_t buffer)
{
    return m_swapchain->GetBackBuffer(buffer);
}

void Context::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandListBox>>& command_lists)
{
    std::vector<std::shared_ptr<CommandList>> raw_command_lists;
    for (auto& command_list : command_lists)
    {
        std::vector<ResourceBarrierDesc> new_barriers;
        auto& barriers = command_list->GetLazyBarriers();
        for (auto& barrier : barriers)
        {
            auto& global_state_tracker = barrier.resource->GetGlobalResourceStateTracker();
            for (uint32_t i = 0; i < barrier.level_count; ++i)
            {
                for (uint32_t j = 0; j < barrier.layer_count; ++j)
                {
                    barrier.state_before = global_state_tracker.GetSubresourceState(barrier.base_mip_level + i, barrier.base_array_layer + j);
                    new_barriers.emplace_back(barrier);
                }
            }
        }
        if (!new_barriers.empty())
        {
            uint32_t cmd_offset = m_tmp_command_lists_offset[GetFrameIndex()]++;
            while (cmd_offset >= m_tmp_command_lists[GetFrameIndex()].size())
            {
                m_tmp_command_lists[GetFrameIndex()].emplace_back(m_device->CreateCommandList());
                m_tmp_fence_values[GetFrameIndex()].emplace_back(0);
            }
            m_fence->Wait(m_tmp_fence_values[GetFrameIndex()][cmd_offset]);
            auto& tmp_cmd = m_tmp_command_lists[GetFrameIndex()][cmd_offset];
            tmp_cmd->Open();
            tmp_cmd->ResourceBarrier(new_barriers);
            tmp_cmd->Close();
            raw_command_lists.emplace_back(tmp_cmd);
            m_tmp_fence_values[GetFrameIndex()][cmd_offset] = m_fence_value + 1;
        }

        raw_command_lists.emplace_back(command_list->GetCommandList());
        command_list->fence_value = m_fence_value + 1;

        auto& state_trackers = command_list->GetResourceStateTrackers();
        for (const auto& state_tracker_pair : state_trackers)
        {
            auto& resource = state_tracker_pair.first;
            auto& state_tracker = state_tracker_pair.second;
            auto& global_state_tracker = resource->GetGlobalResourceStateTracker();
            for (uint32_t i = 0; i < resource->GetLevelCount(); ++i)
            {
                for (uint32_t j = 0; j < resource->GetLayerCount(); ++j)
                {
                    auto state = state_tracker.GetSubresourceState(i, j);
                    if (state != ResourceState::kUnknown)
                        global_state_tracker.SetSubresourceState(i, j, state);
                }
            }
        }
    }
    m_device->ExecuteCommandLists(raw_command_lists);
    m_device->Signal(m_fence, ++m_fence_value);
}

void Context::WaitIdle()
{
    m_device->Signal(m_fence, ++m_fence_value);
    m_fence->Wait(m_fence_value);
}

void Context::Resize(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    m_swapchain.reset();
    m_swapchain = m_device->CreateSwapchain(m_window, m_width, m_height, FrameCount, m_vsync);
    m_frame_index = 0;
}

void Context::Present()
{
    auto& back_buffer = GetBackBuffer(GetFrameIndex());
    auto& global_state_tracker = back_buffer->GetGlobalResourceStateTracker();
    ResourceBarrierDesc barrier = {};
    barrier.resource = back_buffer;
    barrier.state_before = global_state_tracker.GetSubresourceState(0, 0);
    barrier.state_after = ResourceState::kPresent;
    global_state_tracker.SetSubresourceState(0, 0, barrier.state_after);

    m_swapchain_command_list = m_swapchain_command_lists[m_frame_index];
    m_fence->Wait(m_swapchain_fence_values[m_frame_index]);
    m_swapchain_command_list->Open();
    m_swapchain_command_list->ResourceBarrier({ barrier });
    m_swapchain_command_list->Close();

    m_swapchain->NextImage(m_fence, ++m_fence_value);
    m_device->Wait(m_fence, m_fence_value);
    m_device->ExecuteCommandLists({ m_swapchain_command_list });
    m_device->Signal(m_fence, ++m_fence_value);
    m_swapchain_fence_values[m_frame_index] = m_fence_value;
    m_swapchain->Present(m_fence, m_fence_value);

    m_frame_index = (m_frame_index + 1) % FrameCount;
    m_tmp_command_lists_offset[m_frame_index] = 0;
}

std::shared_ptr<Device> Context::GetDevice()
{
    return m_device;
}
