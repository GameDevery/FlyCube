#include "SSAOPass.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <chrono>
#include <random>
#include <Utilities/FormatHelper.h>

inline float lerp(float a, float b, float f)
{
    return a + f * (b - a);
}

SSAOPass::SSAOPass(Context& context, CommandListBox& command_list, const Input& input, int width, int height)
    : m_context(context)
    , m_input(input)
    , m_width(width)
    , m_height(height)
    , m_program(context, std::bind(&SSAOPass::SetDefines, this, std::placeholders::_1))
    , m_program_blur(context)
{
    CreateSizeDependentResources();

    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    int kernel_size = m_program.ps.cbuffer.SSAOBuffer.samples.size();
    for (int i = 0; i < kernel_size; ++i)
    {
        glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = float(i) / float(kernel_size);

        // Scale samples s.t. they're more aligned to center of kernel
        scale = lerp(0.1f, 1.0f, scale * scale);
        sample *= scale;
        m_program.ps.cbuffer.SSAOBuffer.samples[i] = glm::vec4(sample, 1.0f);
    }

    std::vector<glm::vec4> ssaoNoise;
    for (uint32_t i = 0; i < 16; ++i)
    {
        glm::vec4 noise(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f, 0.0f, 0.0f);
        ssaoNoise.push_back(noise);
    }
    m_noise_texture = context.CreateTexture(BindFlag::kShaderResource, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, 1, 4, 4);
    size_t num_bytes = 0;
    size_t row_bytes = 0;
    GetFormatInfo(4, 4, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, num_bytes, row_bytes);
    command_list.UpdateSubresource(m_noise_texture, 0, ssaoNoise.data(), row_bytes, num_bytes);

    if (m_context.IsVariableRateShadingSupported())
    {
        std::vector<ShadingRate> shading_rate;
        uint32_t tile_size = context.GetShadingRateImageTileSize();
        uint32_t shading_rate_width = (width + tile_size - 1) / tile_size;
        uint32_t shading_rate_height = (height + tile_size - 1) / tile_size;
        for (uint32_t i = 0; i < shading_rate_width; ++i)
        {
            for (uint32_t j = 0; j < shading_rate_height; ++j)
            {
                shading_rate.emplace_back(ShadingRate::k2x2);
            }
        }
        m_shading_rate_texture = context.CreateTexture(BindFlag::kShadingRateSource, gli::format::FORMAT_R8_UINT_PACK8, 1, shading_rate_width, shading_rate_height);
        num_bytes = 0;
        row_bytes = 0;
        GetFormatInfo(shading_rate_width, shading_rate_height, gli::format::FORMAT_R8_UINT_PACK8, num_bytes, row_bytes);
        command_list.UpdateSubresource(m_shading_rate_texture, 0, shading_rate.data(), row_bytes, num_bytes);
    }
}

void SSAOPass::OnUpdate()
{
    m_program.ps.cbuffer.SSAOBuffer.ao_radius = m_settings.ao_radius;
    m_program.ps.cbuffer.SSAOBuffer.width = m_width;
    m_program.ps.cbuffer.SSAOBuffer.height = m_height;

    glm::mat4 projection, view, model;
    m_input.camera.GetMatrix(projection, view, model);
    m_program.ps.cbuffer.SSAOBuffer.projection = glm::transpose(projection);
    m_program.ps.cbuffer.SSAOBuffer.view = glm::transpose(view);
    m_program.ps.cbuffer.SSAOBuffer.viewInverse = glm::transpose(glm::transpose(glm::inverse(m_input.camera.GetViewMatrix())));
}

void SSAOPass::OnRender(CommandListBox& command_list)
{
    if (!m_settings.use_ssao)
        return;

    command_list.SetViewport(m_width, m_height);

    command_list.UseProgram(m_program);
    command_list.Attach(m_program.ps.cbv.SSAOBuffer, m_program.ps.cbuffer.SSAOBuffer);

    std::array<float, 4> color = { 0.0f, 0.0f, 0.0f, 1.0f };
    command_list.Attach(m_program.ps.om.rtv0, m_ao);
    command_list.ClearColor(m_program.ps.om.rtv0, color);
    command_list.Attach(m_program.ps.om.dsv, m_depth_stencil_view);
    command_list.ClearDepth(m_program.ps.om.dsv, 1.0f);

    m_input.square.ia.indices.Bind(command_list);
    m_input.square.ia.positions.BindToSlot(command_list, m_program.vs.ia.POSITION);
    m_input.square.ia.texcoords.BindToSlot(command_list, m_program.vs.ia.TEXCOORD);

    if (m_context.IsVariableRateShadingSupported())
    {
        command_list.RSSetShadingRate(ShadingRate::k1x1, std::array<ShadingRateCombiner, 2>{ ShadingRateCombiner::kPassthrough, ShadingRateCombiner::kOverride });
        command_list.RSSetShadingRateImage(m_shading_rate_texture);
    }
    for (auto& range : m_input.square.ia.ranges)
    {
        command_list.Attach(m_program.ps.srv.gPosition, m_input.geometry_pass.position);
        command_list.Attach(m_program.ps.srv.gNormal, m_input.geometry_pass.normal);
        command_list.Attach(m_program.ps.srv.noiseTexture, m_noise_texture);
        command_list.DrawIndexed(range.index_count, range.start_index_location, range.base_vertex_location);
    }
    if (m_context.IsVariableRateShadingSupported())
    {
        command_list.RSSetShadingRateImage({});
        command_list.RSSetShadingRate(ShadingRate::k1x1);
    }

    if (m_settings.use_ao_blur)
    {
        command_list.UseProgram(m_program_blur);
        command_list.Attach(m_program_blur.ps.uav.out_uav, m_ao_blur);

        m_input.square.ia.indices.Bind(command_list);
        m_input.square.ia.positions.BindToSlot(command_list, m_program_blur.vs.ia.POSITION);
        m_input.square.ia.texcoords.BindToSlot(command_list, m_program_blur.vs.ia.TEXCOORD);
        for (auto& range : m_input.square.ia.ranges)
        {
            command_list.Attach(m_program_blur.ps.srv.ssaoInput, m_ao);
            command_list.DrawIndexed(range.index_count, range.start_index_location, range.base_vertex_location);
        }

        output.ao = m_ao_blur;
    }
    else
    {
        output.ao = m_ao;
    }
}

void SSAOPass::OnResize(int width, int height)
{
    m_width = width;
    m_height = height;
    CreateSizeDependentResources();
}

void SSAOPass::CreateSizeDependentResources()
{
    m_ao = m_context.CreateTexture(BindFlag::kRenderTarget | BindFlag::kShaderResource, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, 1, m_width, m_height, 1);
    m_ao_blur = m_context.CreateTexture(BindFlag::kRenderTarget | BindFlag::kShaderResource | BindFlag::kUnorderedAccess, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, 1, m_width, m_height, 1);
    m_depth_stencil_view = m_context.CreateTexture(BindFlag::kDepthStencil, gli::format::FORMAT_D24_UNORM_S8_UINT_PACK32, 1, m_width, m_height, 1);
}

void SSAOPass::OnModifySponzaSettings(const SponzaSettings& settings)
{
    SponzaSettings prev = m_settings;
    m_settings = settings;
    if (prev.msaa_count != m_settings.msaa_count)
    {
        m_program.ps.desc.define["SAMPLE_COUNT"] = std::to_string(m_settings.msaa_count);
        m_program.UpdateProgram();
    }
}

void SSAOPass::SetDefines(ProgramHolder<SSAOPassPS, SSAOPassVS>& program)
{
    if (m_settings.msaa_count != 1)
        program.ps.desc.define["SAMPLE_COUNT"] = std::to_string(m_settings.msaa_count);
}
