#pragma once
#include "Device/Device.h"
#include <CPUDescriptorPool/DXCPUDescriptorPool.h>
#include <GPUDescriptorPool/DXGPUDescriptorPool.h>
#include <dxgi.h>
#include <d3d12.h>
#include <wrl.h>
using namespace Microsoft::WRL;

class DXAdapter;

class DXDevice : public Device
{
public:
    DXDevice(DXAdapter& adapter);
    std::shared_ptr<Swapchain> CreateSwapchain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t frame_count, bool vsync) override;
    std::shared_ptr<CommandList> CreateCommandList() override;
    std::shared_ptr<Fence> CreateFence() override;
    std::shared_ptr<Semaphore> CreateGPUSemaphore() override;
    std::shared_ptr<Resource> CreateTexture(uint32_t bind_flag, gli::format format, uint32_t msaa_count, int width, int height, int depth, int mip_levels) override;
    std::shared_ptr<Resource> CreateBuffer(uint32_t bind_flag, uint32_t buffer_size, uint32_t stride) override;
    std::shared_ptr<Resource> CreateSampler(const SamplerDesc& desc) override;
    std::shared_ptr<View> CreateView(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc) override;
    std::shared_ptr<Framebuffer> CreateFramebuffer(const std::shared_ptr<Pipeline>& pipeline, const std::vector<std::shared_ptr<View>>& rtvs, const std::shared_ptr<View>& dsv) override;
    std::shared_ptr<Shader> CompileShader(const ShaderDesc& desc) override;
    std::shared_ptr<Program> CreateProgram(const std::vector<std::shared_ptr<Shader>>& shaders) override;
    std::shared_ptr<Pipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    std::shared_ptr<Pipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override;
    std::shared_ptr<Resource> CreateBottomLevelAS(const std::shared_ptr<CommandList>& command_list, const BufferDesc& vertex, const BufferDesc& index) override;
    std::shared_ptr<Resource> CreateTopLevelAS(const std::shared_ptr<CommandList>& command_list, const std::vector<std::pair<std::shared_ptr<Resource>, glm::mat4>>& geometry) override;
    bool IsDxrSupported() const override;
    void Wait(const std::shared_ptr<Semaphore>& semaphore) override;
    void Signal(const std::shared_ptr<Semaphore>& semaphore) override;
    void ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& command_lists, const std::shared_ptr<Fence>& fence) override;

    DXAdapter& GetAdapter();
    ComPtr<ID3D12Device> GetDevice();
    ComPtr<ID3D12CommandQueue> GetCommandQueue();
    DXCPUDescriptorPool& GetCPUDescriptorPool();
    DXGPUDescriptorPool& GetGPUDescriptorPool();

private:
    DXAdapter& m_adapter;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_command_queue;
    DXCPUDescriptorPool m_cpu_descriptor_pool;
    DXGPUDescriptorPool m_gpu_descriptor_pool;
    bool m_is_dxr_supported = false;
};
