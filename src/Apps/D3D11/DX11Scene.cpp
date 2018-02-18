#include "DX11Scene.h"
#include "DX11CreateUtils.h"
#include <Utilities/DXUtility.h>
#include <Utilities/FileUtility.h>
#include <Utilities/State.h>
#include <D3Dcompiler.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/gtx/transform.hpp>

DX11Scene::DX11Scene(GLFWwindow* window, int width, int height)
    : m_width(width)
    , m_height(height)
    , m_context(window, m_width, m_height)
    , m_model_square(m_context, "model/square.obj")
    , m_geometry_pass(m_context, { m_scene_list, m_camera }, width, height)
    , m_shadow_pass(m_context, { m_scene_list, m_camera, light_pos }, width, height)
    , m_ssao_pass(m_context, { m_geometry_pass.output, m_model_square, m_camera }, width, height)
    , m_light_pass(m_context, { m_geometry_pass.output, m_shadow_pass.output, m_ssao_pass.output, m_model_square, m_camera, light_pos }, width, height)
    , m_compute_luminance(m_context, { m_light_pass.output.srv, m_model_square, m_render_target_view, m_depth_stencil_view }, width, height)
    , m_imgui_pass(m_context, { *this, m_render_target_view, m_depth_stencil_view }, width, height)
{
    // prevent a call ~aiScene 
    m_scene_list.reserve(2);
    m_scene_list.emplace_back(m_context, "model/sponza/sponza.obj");
    m_scene_list.back().matrix = glm::scale(glm::vec3(0.01f));
    m_scene_list.emplace_back(m_context, "model/Mannequin_Animation/source/Mannequin_Animation.FBX");
    m_scene_list.back().matrix = glm::scale(glm::vec3(0.07f)) * glm::translate(glm::vec3(75.0f, 0.0f, 0.0f)) * glm::rotate(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    CreateRT();
    CreateViewPort();
    CreateSampler();

    m_camera.SetViewport(m_width, m_height);
    m_context.device_context->PSSetSamplers(0, 1, m_texture_sampler.GetAddressOf());
    m_context.device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

IScene::Ptr DX11Scene::Create(GLFWwindow* window, int width, int height)
{
    return std::make_unique<DX11Scene>(window, width, height);
}

void DX11Scene::OnUpdate()
{
    UpdateCameraMovement();

    static float angle = 0.0f;
    static std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
    std::chrono::time_point<std::chrono::system_clock> end = std::chrono::system_clock::now();
    int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    start = end;

    auto& state = CurState<bool>::Instance().state;
    if (state["pause"])
        angle += elapsed / 2e6f;

    float light_r = 2.5;
    light_pos = glm::vec3(light_r * cos(angle), 25.0f, light_r * sin(angle));

    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnUpdate();
    }

    m_geometry_pass.OnUpdate();
    m_shadow_pass.OnUpdate();
    m_ssao_pass.OnUpdate();
    m_light_pass.OnUpdate();
}

void DX11Scene::OnRender()
{
    m_context.device_context->RSSetViewports(1, &m_viewport);

    m_context.perf->BeginEvent(L"Geometry Pass");
    m_geometry_pass.OnRender();
    m_context.perf->EndEvent();

    m_context.perf->BeginEvent(L"Shadow Pass");
    m_shadow_pass.OnRender();
    m_context.perf->EndEvent();

    m_context.device_context->RSSetViewports(1, &m_viewport);

    m_context.perf->BeginEvent(L"SSAO Pass");
    m_ssao_pass.OnRender();
    m_context.perf->EndEvent();

    m_context.perf->BeginEvent(L"Light Pass");
    m_light_pass.OnRender();
    m_context.perf->EndEvent();

    m_context.perf->BeginEvent(L"HDR Pass");
    m_compute_luminance.OnRender();
    m_context.perf->EndEvent();

    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_context.perf->BeginEvent(L"ImGui Pass");
        m_imgui_pass.OnRender();
        m_context.perf->EndEvent();
    }

    ASSERT_SUCCEEDED(m_context.swap_chain->Present(0, 0));
}

void DX11Scene::OnResize(int width, int height)
{
    if (width == m_width && height == m_height)
        return;

    m_width = width;
    m_height = height;

    m_render_target_view.Reset();

    DXGI_SWAP_CHAIN_DESC desc = {};
    ASSERT_SUCCEEDED(m_context.swap_chain->GetDesc(&desc));
    ASSERT_SUCCEEDED(m_context.swap_chain->ResizeBuffers(1, width, height, desc.BufferDesc.Format, desc.Flags));

    CreateRT();
    CreateViewPort();
    m_context.device_context->RSSetViewports(1, &m_viewport);
    m_camera.SetViewport(m_width, m_height);

    m_geometry_pass.OnResize(width, height);
    m_shadow_pass.OnResize(width, height);
    m_light_pass.OnResize(width, height);
    m_imgui_pass.OnResize(width, height);
}

void DX11Scene::OnKey(int key, int action)
{
    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnKey(key, action);
        return;
    }

    if (action == GLFW_PRESS)
        m_keys[key] = true;
    else if (action == GLFW_RELEASE)
        m_keys[key] = false;

    if (key == GLFW_KEY_N && action == GLFW_PRESS)
    {
        auto & state = CurState<bool>::Instance().state;
        state["disable_norm"] = !state["disable_norm"];
    }

    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        auto & state = CurState<bool>::Instance().state;
        state["pause"] = !state["pause"];
    }

    if (key == GLFW_KEY_J && action == GLFW_PRESS)
    {
        auto & state = CurState<bool>::Instance().state;
        state["no_shadow_discard"] = !state["no_shadow_discard"];
    }
}

void DX11Scene::OnMouse(bool first_event, double xpos, double ypos)
{
    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnMouse(first_event, xpos, ypos);
        return;
    }

    if (first_event)
    {
        m_last_x = xpos;
        m_last_y = ypos;
    }

    double xoffset = xpos - m_last_x;
    double yoffset = m_last_y - ypos;

    m_last_x = xpos;
    m_last_y = ypos;

    m_camera.ProcessMouseMovement((float)xoffset, (float)yoffset);
}

void DX11Scene::OnMouseButton(int button, int action)
{
    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnMouseButton(button, action);
    }
}

void DX11Scene::OnScroll(double xoffset, double yoffset)
{
    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnScroll(xoffset, yoffset);
    }
}

void DX11Scene::OnInputChar(unsigned int ch)
{
    if (glfwGetInputMode(m_context.window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        m_imgui_pass.OnInputChar(ch);
    }
}

void DX11Scene::OnModifySettings(const Settings& settings)
{
    m_geometry_pass.OnModifySettings(settings);
    m_light_pass.OnModifySettings(settings);
    m_compute_luminance.OnModifySettings(settings);
    m_shadow_pass.OnModifySettings(settings);
    m_ssao_pass.OnModifySettings(settings);
}

void DX11Scene::CreateRT()
{
    ComPtr<ID3D11Texture2D> back_buffer;
    ASSERT_SUCCEEDED(m_context.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer));
    ASSERT_SUCCEEDED(m_context.device->CreateRenderTargetView(back_buffer.Get(), nullptr, &m_render_target_view));
    CreateDsv(m_context, 1, m_width, m_height, m_depth_stencil_view);
}

void DX11Scene::CreateViewPort()
{
    ZeroMemory(&m_viewport, sizeof(D3D11_VIEWPORT));
    m_viewport.TopLeftX = 0;
    m_viewport.TopLeftY = 0;
    m_viewport.Width = m_width;
    m_viewport.Height = m_height;
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
}

void DX11Scene::CreateSampler()
{
    D3D11_SAMPLER_DESC samp_desc = {};
    samp_desc.Filter = D3D11_FILTER_ANISOTROPIC;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp_desc.MinLOD = 0;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;

    ASSERT_SUCCEEDED(m_context.device->CreateSamplerState(&samp_desc, &m_texture_sampler));
}
