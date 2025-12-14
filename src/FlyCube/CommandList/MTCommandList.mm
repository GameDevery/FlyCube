#include "CommandList/MTCommandList.h"

#include "BindingSet/MTBindingSet.h"
#include "Device/MTDevice.h"
#include "Pipeline/MTComputePipeline.h"
#include "Pipeline/MTGraphicsPipeline.h"
#include "QueryHeap/MTQueryHeap.h"
#include "Resource/MTResource.h"
#include "Utilities/Cast.h"
#include "Utilities/Logging.h"
#include "Utilities/NotReached.h"
#include "View/MTView.h"

namespace {

MTLIndexType ConvertIndexType(gli::format format)
{
    switch (format) {
    case gli::format::FORMAT_R16_UINT_PACK16:
        return MTLIndexTypeUInt16;
    case gli::format::FORMAT_R32_UINT_PACK32:
        return MTLIndexTypeUInt32;
    default:
        NOTREACHED();
    }
}

MTLLoadAction ConvertLoadAction(RenderPassLoadOp op)
{
    switch (op) {
    case RenderPassLoadOp::kLoad:
        return MTLLoadActionLoad;
    case RenderPassLoadOp::kClear:
        return MTLLoadActionClear;
    case RenderPassLoadOp::kDontCare:
        return MTLLoadActionDontCare;
    default:
        NOTREACHED();
    }
}

MTLStoreAction ConvertStoreAction(RenderPassStoreOp op)
{
    switch (op) {
    case RenderPassStoreOp::kStore:
        return MTLStoreActionStore;
    case RenderPassStoreOp::kDontCare:
        return MTLStoreActionDontCare;
    default:
        NOTREACHED();
    }
}

MTLTriangleFillMode ConvertFillMode(FillMode fill_mode)
{
    switch (fill_mode) {
    case FillMode::kSolid:
        return MTLTriangleFillModeFill;
    case FillMode::kWireframe:
        return MTLTriangleFillModeLines;
    default:
        NOTREACHED();
    }
}

MTLCullMode ConvertCullMode(CullMode cull_mode)
{
    switch (cull_mode) {
    case CullMode::kNone:
        return MTLCullModeNone;
    case CullMode::kFront:
        return MTLCullModeFront;
    case CullMode::kBack:
        return MTLCullModeBack;
    default:
        NOTREACHED();
    }
}

MTLWinding ConvertFrontFace(FrontFace front_face)
{
    switch (front_face) {
    case FrontFace::kClockwise:
        return MTLWindingClockwise;
    case FrontFace::kCounterClockwise:
        return MTLWindingCounterClockwise;
    default:
        NOTREACHED();
    }
}

id<MTL4ArgumentTable> CreateArgumentTable(MTDevice& device)
{
    MTL4ArgumentTableDescriptor* argument_table_descriptor = [MTL4ArgumentTableDescriptor new];
    argument_table_descriptor.initializeBindings = true;
    argument_table_descriptor.maxBufferBindCount = device.GetMaxPerStageBufferCount();
    argument_table_descriptor.maxSamplerStateBindCount = 16;
    argument_table_descriptor.maxTextureBindCount = 128;

    NSError* error = nullptr;
    id<MTL4ArgumentTable> argument_table = [device.GetDevice() newArgumentTableWithDescriptor:argument_table_descriptor
                                                                                        error:&error];
    if (!argument_table) {
        Logging::Println("Failed to create MTL4ArgumentTable: {}", error);
    }
    return argument_table;
}

MTLStages ResourceStateToMTLStages(ResourceState state)
{
    MTLStages stages = 0;

    if (state &
        (ResourceState::kVertexAndConstantBuffer | ResourceState::kIndexBuffer | ResourceState::kUnorderedAccess |
         ResourceState::kNonPixelShaderResource | ResourceState::kIndirectArgument)) {
        stages |= MTLStageVertex | MTLStageObject | MTLStageMesh;
    }

    if (state &
        (ResourceState::kVertexAndConstantBuffer | ResourceState::kRenderTarget | ResourceState::kUnorderedAccess |
         ResourceState::kDepthStencilWrite | ResourceState::kDepthStencilRead | ResourceState::kPixelShaderResource |
         ResourceState::kIndirectArgument | ResourceState::kShadingRateSource)) {
        stages |= MTLStageFragment;
    }

    if (state & (ResourceState::kVertexAndConstantBuffer | ResourceState::kUnorderedAccess |
                 ResourceState::kNonPixelShaderResource | ResourceState::kIndirectArgument)) {
        stages |= MTLStageDispatch;
    }

    if (state & (ResourceState::kCopyDest | ResourceState::kCopySource)) {
        stages |= MTLStageBlit;
    }

    if (state & (ResourceState::kRaytracingAccelerationStructure)) {
        stages |= MTLStageAccelerationStructure;
    }

    return stages;
}

} // namespace

MTCommandList::MTCommandList(MTDevice& device, CommandListType type)
    : device_(device)
{
    Reset();
}

void MTCommandList::Reset()
{
    Close();

    if (!allocator_) {
        allocator_ = [device_.GetDevice() newCommandAllocator];
    } else {
        [allocator_ reset];
    }
    if (!command_buffer_) {
        command_buffer_ = [device_.GetDevice() newCommandBuffer];
    }
    [command_buffer_ beginCommandBufferWithAllocator:allocator_];

    patch_buffers_ = {};
    state_ = std::make_unique<State>();
    CreateArgumentTables();
    state_->residency_set = device_.CreateResidencySet();
    [command_buffer_ useResidencySet:state_->residency_set];
}

void MTCommandList::Close()
{
    if (!state_) {
        return;
    }

    CloseComputeEncoder();

    [command_buffer_ endCommandBuffer];
    [state_->residency_set commit];
    state_.reset();
}

void MTCommandList::BindPipeline(const std::shared_ptr<Pipeline>& pipeline)
{
    state_->pipeline = pipeline;
    state_->need_apply_pipeline = true;
    state_->need_apply_binding_set = true;
}

void MTCommandList::BindBindingSet(const std::shared_ptr<BindingSet>& binding_set)
{
    state_->binding_set = std::static_pointer_cast<MTBindingSet>(binding_set);
    state_->need_apply_binding_set = true;
}

void MTCommandList::BeginRenderPass(const RenderPassDesc& render_pass_desc)
{
    CloseComputeEncoder();

    auto add_attachment = [&](auto& attachment, RenderPassLoadOp load_op, RenderPassStoreOp store_op,
                              const std::shared_ptr<View>& view) {
        if (!view) {
            return;
        }
        attachment.loadAction = ConvertLoadAction(load_op);
        attachment.storeAction = ConvertStoreAction(store_op);

        decltype(auto) mt_view = CastToImpl<MTView>(view);
        attachment.level = mt_view->GetBaseMipLevel();
        attachment.slice = mt_view->GetBaseArrayLayer();
        attachment.texture = mt_view->GetTexture();

        if (attachment.texture) {
            AddAllocation(attachment.texture);
        }
    };

    MTL4RenderPassDescriptor* render_pass_descriptor = [MTL4RenderPassDescriptor new];

    for (size_t i = 0; i < render_pass_desc.colors.size(); ++i) {
        decltype(auto) color_attachment = render_pass_descriptor.colorAttachments[i];
        add_attachment(color_attachment, render_pass_desc.colors[i].load_op, render_pass_desc.colors[i].store_op,
                       render_pass_desc.colors[i].view);
        if (render_pass_desc.colors[i].load_op == RenderPassLoadOp::kClear) {
            color_attachment.clearColor =
                MTLClearColorMake(render_pass_desc.colors[i].clear_value[0], render_pass_desc.colors[i].clear_value[1],
                                  render_pass_desc.colors[i].clear_value[2], render_pass_desc.colors[i].clear_value[3]);
        }
    }

    decltype(auto) depth_attachment = render_pass_descriptor.depthAttachment;
    if (render_pass_desc.depth_stencil_view &&
        gli::is_depth(render_pass_desc.depth_stencil_view->GetResource()->GetFormat())) {
        add_attachment(depth_attachment, render_pass_desc.depth.load_op, render_pass_desc.depth.store_op,
                       render_pass_desc.depth_stencil_view);
    }
    depth_attachment.clearDepth = render_pass_desc.depth.clear_value;

    decltype(auto) stencil_attachment = render_pass_descriptor.stencilAttachment;
    if (render_pass_desc.depth_stencil_view &&
        gli::is_stencil(render_pass_desc.depth_stencil_view->GetResource()->GetFormat())) {
        add_attachment(stencil_attachment, render_pass_desc.stencil.load_op, render_pass_desc.stencil.store_op,
                       render_pass_desc.depth_stencil_view);
    }
    stencil_attachment.clearStencil = render_pass_desc.stencil.clear_value;

    render_pass_descriptor.renderTargetWidth = render_pass_desc.render_area.x + render_pass_desc.render_area.width;
    render_pass_descriptor.renderTargetHeight = render_pass_desc.render_area.y + render_pass_desc.render_area.height;
    render_pass_descriptor.renderTargetArrayLength = render_pass_desc.layers;
    render_pass_descriptor.defaultRasterSampleCount = render_pass_desc.sample_count;

    state_->render_encoder = [command_buffer_ renderCommandEncoderWithDescriptor:render_pass_descriptor];
    if (state_->render_encoder == nullptr) {
        Logging::Println("Failed to create MTL4RenderCommandEncoder");
    }

    [state_->render_encoder setViewport:state_->viewport];
    [state_->render_encoder setScissorRect:state_->scissor];
    if (state_->min_depth_bounds != 0.0 || state_->max_depth_bounds != 1.0) {
        [state_->render_encoder setDepthTestMinBound:state_->min_depth_bounds maxBound:state_->max_depth_bounds];
    }
    if (state_->stencil_reference) {
        [state_->render_encoder setStencilReferenceValue:state_->stencil_reference];
    }
    if (state_->blend_constants) {
        [state_->render_encoder setBlendColorRed:state_->blend_constants.value()[0]
                                           green:state_->blend_constants.value()[1]
                                            blue:state_->blend_constants.value()[2]
                                           alpha:state_->blend_constants.value()[3]];
    }
    [state_->render_encoder setArgumentTable:state_->argument_tables.at(ShaderType::kVertex)
                                    atStages:MTLRenderStageVertex];
    [state_->render_encoder setArgumentTable:state_->argument_tables.at(ShaderType::kPixel)
                                    atStages:MTLRenderStageFragment];
    [state_->render_encoder setArgumentTable:state_->argument_tables.at(ShaderType::kAmplification)
                                    atStages:MTLRenderStageObject];
    [state_->render_encoder setArgumentTable:state_->argument_tables.at(ShaderType::kMesh) atStages:MTLRenderStageMesh];

    state_->need_apply_pipeline = true;
}

void MTCommandList::EndRenderPass()
{
    [state_->render_encoder endEncoding];
    state_->render_encoder = nullptr;
}

void MTCommandList::BeginEvent(const std::string& name) {}

void MTCommandList::EndEvent() {}

void MTCommandList::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    ApplyGraphicsState();
    [state_->render_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                               vertexStart:first_vertex
                               vertexCount:vertex_count
                             instanceCount:instance_count
                              baseInstance:first_instance];
}

void MTCommandList::DrawIndexed(uint32_t index_count,
                                uint32_t instance_count,
                                uint32_t first_index,
                                int32_t vertex_offset,
                                uint32_t first_instance)
{
    ApplyGraphicsState();
    MTLIndexType index_format = ConvertIndexType(state_->index_format);
    const uint32_t index_stride = index_format == MTLIndexTypeUInt32 ? 4 : 2;
    [state_->render_encoder
        drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                   indexCount:index_count
                    indexType:index_format
                  indexBuffer:state_->index_buffer.gpuAddress + state_->index_buffer_offset + index_stride * first_index
            indexBufferLength:state_->index_buffer.length - state_->index_buffer_offset - index_stride * first_index
                instanceCount:instance_count
                   baseVertex:vertex_offset
                 baseInstance:first_instance];
}

void MTCommandList::DrawIndirect(const std::shared_ptr<Resource>& argument_buffer, uint64_t argument_buffer_offset)
{
    ApplyGraphicsState();
    decltype(auto) mt_argument_buffer = CastToImpl<MTResource>(argument_buffer)->GetBuffer();
    AddAllocation(mt_argument_buffer);
    [state_->render_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            indirectBuffer:mt_argument_buffer.gpuAddress + argument_buffer_offset];
}

void MTCommandList::DrawIndexedIndirect(const std::shared_ptr<Resource>& argument_buffer,
                                        uint64_t argument_buffer_offset)
{
    ApplyGraphicsState();
    decltype(auto) mt_argument_buffer = CastToImpl<MTResource>(argument_buffer)->GetBuffer();
    AddAllocation(mt_argument_buffer);
    MTLIndexType index_format = ConvertIndexType(state_->index_format);
    [state_->render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexType:index_format
                                      indexBuffer:state_->index_buffer.gpuAddress + state_->index_buffer_offset
                                indexBufferLength:state_->index_buffer.length - state_->index_buffer_offset
                                   indirectBuffer:mt_argument_buffer.gpuAddress + argument_buffer_offset];
}

void MTCommandList::DrawIndirectCount(const std::shared_ptr<Resource>& argument_buffer,
                                      uint64_t argument_buffer_offset,
                                      const std::shared_ptr<Resource>& count_buffer,
                                      uint64_t count_buffer_offset,
                                      uint32_t max_draw_count,
                                      uint32_t stride)
{
    NOTREACHED();
}

void MTCommandList::DrawIndexedIndirectCount(const std::shared_ptr<Resource>& argument_buffer,
                                             uint64_t argument_buffer_offset,
                                             const std::shared_ptr<Resource>& count_buffer,
                                             uint64_t count_buffer_offset,
                                             uint32_t max_draw_count,
                                             uint32_t stride)
{
    NOTREACHED();
}

void MTCommandList::Dispatch(uint32_t thread_group_count_x,
                             uint32_t thread_group_count_y,
                             uint32_t thread_group_count_z)
{
    decltype(auto) mt_pipeline = CastToImpl<MTComputePipeline>(state_->pipeline);
    MTLSize threadgroups_per_grid = { thread_group_count_x, thread_group_count_y, thread_group_count_z };

    OpenComputeEncoder();
    ApplyComputeState();
    AddComputeBarriers();
    [state_->compute_encoder dispatchThreadgroups:threadgroups_per_grid
                            threadsPerThreadgroup:mt_pipeline->GetNumthreads()];
}

void MTCommandList::DispatchIndirect(const std::shared_ptr<Resource>& argument_buffer, uint64_t argument_buffer_offset)
{
    decltype(auto) mt_argument_buffer = CastToImpl<MTResource>(argument_buffer)->GetBuffer();
    decltype(auto) mt_pipeline = CastToImpl<MTComputePipeline>(state_->pipeline);
    AddAllocation(mt_argument_buffer);

    OpenComputeEncoder();
    ApplyComputeState();
    AddComputeBarriers();
    [state_->compute_encoder
        dispatchThreadgroupsWithIndirectBuffer:mt_argument_buffer.gpuAddress + argument_buffer_offset
                         threadsPerThreadgroup:mt_pipeline->GetNumthreads()];
}

void MTCommandList::DispatchMesh(uint32_t thread_group_count_x,
                                 uint32_t thread_group_count_y,
                                 uint32_t thread_group_count_z)
{
    ApplyGraphicsState();
    decltype(auto) mt_pipeline = CastToImpl<MTGraphicsPipeline>(state_->pipeline);
    [state_->render_encoder
               drawMeshThreadgroups:MTLSizeMake(thread_group_count_x, thread_group_count_y, thread_group_count_z)
        threadsPerObjectThreadgroup:mt_pipeline->GetAmplificationNumthreads()
          threadsPerMeshThreadgroup:mt_pipeline->GetMeshNumthreads()];
}

void MTCommandList::DispatchRays(const RayTracingShaderTables& shader_tables,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t depth)
{
    NOTREACHED();
}

void MTCommandList::ResourceBarrier(const std::vector<ResourceBarrierDesc>& barriers)
{
    for (const auto& barrier : barriers) {
        state_->render_barrier_after_stages |= ResourceStateToMTLStages(barrier.state_after);
        state_->render_barrier_before_stages |= ResourceStateToMTLStages(barrier.state_before);
        state_->compute_barrier_after_stages |= ResourceStateToMTLStages(barrier.state_after);
        state_->compute_barrier_before_stages |= ResourceStateToMTLStages(barrier.state_before);
    }
}

void MTCommandList::UAVResourceBarrier(const std::shared_ptr<Resource>& /*resource*/)
{
    state_->render_barrier_after_stages |= MTLStageVertex | MTLStageObject | MTLStageMesh | MTLStageFragment;
    state_->render_barrier_before_stages |= MTLStageVertex | MTLStageObject | MTLStageMesh | MTLStageFragment;
    state_->compute_barrier_after_stages |= MTLStageDispatch;
    state_->compute_barrier_before_stages |= MTLStageDispatch;
}

void MTCommandList::SetViewport(float x, float y, float width, float height, float min_depth, float max_depth)
{
    state_->viewport.originX = x;
    state_->viewport.originY = y;
    state_->viewport.width = width;
    state_->viewport.height = height;
    state_->viewport.znear = min_depth;
    state_->viewport.zfar = max_depth;

    if (!state_->render_encoder) {
        return;
    }

    [state_->render_encoder setViewport:state_->viewport];
}

void MTCommandList::SetScissorRect(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
    state_->scissor.x = left;
    state_->scissor.y = top;
    state_->scissor.width = right - left;
    state_->scissor.height = bottom - top;

    if (!state_->render_encoder) {
        return;
    }

    [state_->render_encoder setScissorRect:state_->scissor];
}

void MTCommandList::IASetIndexBuffer(const std::shared_ptr<Resource>& resource, uint64_t offset, gli::format format)
{
    decltype(auto) index_buffer = CastToImpl<MTResource>(resource)->GetBuffer();
    AddAllocation(index_buffer);
    state_->index_buffer = index_buffer;
    state_->index_buffer_offset = offset;
    state_->index_format = format;
}

void MTCommandList::IASetVertexBuffer(uint32_t slot, const std::shared_ptr<Resource>& resource, uint64_t offset)
{
    decltype(auto) vertex = CastToImpl<MTResource>(resource)->GetBuffer();
    AddAllocation(vertex);
    uint32_t index = device_.GetMaxPerStageBufferCount() - slot - 1;
    [state_->argument_tables.at(ShaderType::kVertex) setAddress:vertex.gpuAddress + offset atIndex:index];
}

void MTCommandList::RSSetShadingRate(ShadingRate shading_rate, const std::array<ShadingRateCombiner, 2>& combiners)
{
    NOTREACHED();
}

void MTCommandList::SetDepthBounds(float min_depth_bounds, float max_depth_bounds)
{
    state_->min_depth_bounds = min_depth_bounds;
    state_->max_depth_bounds = max_depth_bounds;

    if (!state_->render_encoder) {
        return;
    }

    if (state_->min_depth_bounds != 0.0 || state_->max_depth_bounds != 1.0) {
        [state_->render_encoder setDepthTestMinBound:state_->min_depth_bounds maxBound:state_->max_depth_bounds];
    }
}

void MTCommandList::SetStencilReference(uint32_t stencil_reference)
{
    state_->stencil_reference = stencil_reference;

    if (!state_->render_encoder) {
        return;
    }

    [state_->render_encoder setStencilReferenceValue:state_->stencil_reference];
}

void MTCommandList::SetBlendConstants(float red, float green, float blue, float alpha)
{
    state_->blend_constants = { red, green, blue, alpha };

    if (!state_->render_encoder) {
        return;
    }

    [state_->render_encoder setBlendColorRed:state_->blend_constants.value()[0]
                                       green:state_->blend_constants.value()[1]
                                        blue:state_->blend_constants.value()[2]
                                       alpha:state_->blend_constants.value()[3]];
}

void MTCommandList::BuildBottomLevelAS(const std::shared_ptr<Resource>& src,
                                       const std::shared_ptr<Resource>& dst,
                                       const std::shared_ptr<Resource>& scratch,
                                       uint64_t scratch_offset,
                                       const std::vector<RaytracingGeometryDesc>& descs,
                                       BuildAccelerationStructureFlags flags)
{
    NSMutableArray* geometry_descs = [NSMutableArray array];
    for (const auto& desc : descs) {
        MTL4AccelerationStructureTriangleGeometryDescriptor* geometry_desc =
            FillRaytracingGeometryDesc(desc.vertex, desc.index, desc.flags);
        [geometry_descs addObject:geometry_desc];
    }

    MTL4PrimitiveAccelerationStructureDescriptor* acceleration_structure_desc =
        [MTL4PrimitiveAccelerationStructureDescriptor new];
    acceleration_structure_desc.geometryDescriptors = geometry_descs;

    decltype(auto) mt_dst = CastToImpl<MTResource>(dst);
    decltype(auto) mt_scratch = CastToImpl<MTResource>(scratch);
    AddAllocation(mt_dst->GetAccelerationStructure());
    AddAllocation(mt_scratch->GetBuffer());

    OpenComputeEncoder();
    AddComputeBarriers();
    [state_->compute_encoder
        buildAccelerationStructure:mt_dst->GetAccelerationStructure()
                        descriptor:acceleration_structure_desc
                     scratchBuffer:MTL4BufferRangeMake(mt_scratch->GetBuffer().gpuAddress + scratch_offset,
                                                       mt_scratch->GetBuffer().length - scratch_offset)];
}

// TODO: patch on GPU
id<MTLBuffer> MTCommandList::PatchInstanceData(const std::shared_ptr<Resource>& instance_data,
                                               uint64_t instance_offset,
                                               uint32_t instance_count)
{
    MTLResourceOptions buffer_options = MTLStorageModeShared << MTLResourceStorageModeShift;
    NSUInteger buffer_size = instance_count * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    id<MTLBuffer> buffer = [device_.GetDevice() newBufferWithLength:buffer_size options:buffer_options];
    AddAllocation(buffer);

    uint8_t* instance_ptr =
        static_cast<uint8_t*>(CastToImpl<MTResource>(instance_data)->GetBuffer().contents) + instance_offset;
    uint8_t* patched_instance_ptr = static_cast<uint8_t*>(buffer.contents);
    for (uint32_t i = 0; i < instance_count; ++i) {
        RaytracingGeometryInstance& instance = reinterpret_cast<RaytracingGeometryInstance*>(instance_ptr)[i];
        MTLIndirectAccelerationStructureInstanceDescriptor& patched_instance =
            reinterpret_cast<MTLIndirectAccelerationStructureInstanceDescriptor*>(patched_instance_ptr)[i];

        for (size_t j = 0; j < std::size(instance.transform); ++j) {
            for (size_t k = 0; k < std::size(instance.transform[j]); ++k) {
                patched_instance.transformationMatrix[k][j] = instance.transform[j][k];
            }
        }
        patched_instance.options = static_cast<MTLAccelerationStructureInstanceOptions>(instance.flags);
        patched_instance.mask = instance.instance_mask;
        patched_instance.userID = instance.instance_id;
        patched_instance.accelerationStructureID = { instance.acceleration_structure_handle };
    }
    return buffer;
}

void MTCommandList::BuildTopLevelAS(const std::shared_ptr<Resource>& src,
                                    const std::shared_ptr<Resource>& dst,
                                    const std::shared_ptr<Resource>& scratch,
                                    uint64_t scratch_offset,
                                    const std::shared_ptr<Resource>& instance_data,
                                    uint64_t instance_offset,
                                    uint32_t instance_count,
                                    BuildAccelerationStructureFlags flags)
{
    MTL4InstanceAccelerationStructureDescriptor* acceleration_structure_desc =
        [MTL4InstanceAccelerationStructureDescriptor new];
    acceleration_structure_desc.instanceCount = instance_count;
    id<MTLBuffer> patched_instance_data = PatchInstanceData(instance_data, instance_offset, instance_count);
    acceleration_structure_desc.instanceDescriptorBuffer =
        MTL4BufferRangeMake(patched_instance_data.gpuAddress, patched_instance_data.length);
    acceleration_structure_desc.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
    patch_buffers_.push_back(patched_instance_data);

    decltype(auto) mt_dst = CastToImpl<MTResource>(dst);
    decltype(auto) mt_scratch = CastToImpl<MTResource>(scratch);
    AddAllocation(mt_dst->GetAccelerationStructure());
    AddAllocation(mt_scratch->GetBuffer());

    OpenComputeEncoder();
    AddComputeBarriers();
    [state_->compute_encoder
        buildAccelerationStructure:mt_dst->GetAccelerationStructure()
                        descriptor:acceleration_structure_desc
                     scratchBuffer:MTL4BufferRangeMake(mt_scratch->GetBuffer().gpuAddress + scratch_offset,
                                                       mt_scratch->GetBuffer().length - scratch_offset)];
}

void MTCommandList::CopyAccelerationStructure(const std::shared_ptr<Resource>& src,
                                              const std::shared_ptr<Resource>& dst,
                                              CopyAccelerationStructureMode mode)
{
    OpenComputeEncoder();
    AddComputeBarriers();
    id<MTLAccelerationStructure> mt_src_acceleration_structure =
        CastToImpl<MTResource>(src)->GetAccelerationStructure();
    id<MTLAccelerationStructure> mt_dst_acceleration_structure =
        CastToImpl<MTResource>(dst)->GetAccelerationStructure();
    AddAllocation(mt_src_acceleration_structure);
    AddAllocation(mt_dst_acceleration_structure);
    switch (mode) {
    case CopyAccelerationStructureMode::kClone:
        [state_->compute_encoder copyAccelerationStructure:mt_src_acceleration_structure
                                   toAccelerationStructure:mt_dst_acceleration_structure];
        break;
    case CopyAccelerationStructureMode::kCompact:
        [state_->compute_encoder copyAndCompactAccelerationStructure:mt_src_acceleration_structure
                                             toAccelerationStructure:mt_dst_acceleration_structure];
        break;
    default:
        NOTREACHED();
    }
}

void MTCommandList::CopyBuffer(const std::shared_ptr<Resource>& src_buffer,
                               const std::shared_ptr<Resource>& dst_buffer,
                               const std::vector<BufferCopyRegion>& regions)
{
    OpenComputeEncoder();
    decltype(auto) mt_src_buffer = CastToImpl<MTResource>(src_buffer);
    decltype(auto) mt_dst_buffer = CastToImpl<MTResource>(dst_buffer);
    AddAllocation(mt_src_buffer->GetBuffer());
    AddAllocation(mt_dst_buffer->GetBuffer());
    AddComputeBarriers();
    for (const auto& region : regions) {
        [state_->compute_encoder copyFromBuffer:mt_src_buffer->GetBuffer()
                                   sourceOffset:region.src_offset
                                       toBuffer:mt_dst_buffer->GetBuffer()
                              destinationOffset:region.dst_offset
                                           size:region.num_bytes];
    }
}

void MTCommandList::CopyBufferToTexture(const std::shared_ptr<Resource>& src_buffer,
                                        const std::shared_ptr<Resource>& dst_texture,
                                        const std::vector<BufferTextureCopyRegion>& regions)
{
    CopyBufferTextureImpl(/*buffer_src=*/true, src_buffer, dst_texture, regions);
}

void MTCommandList::CopyTextureToBuffer(const std::shared_ptr<Resource>& src_texture,
                                        const std::shared_ptr<Resource>& dst_buffer,
                                        const std::vector<BufferTextureCopyRegion>& regions)
{
    CopyBufferTextureImpl(/*buffer_src=*/false, dst_buffer, src_texture, regions);
}

void MTCommandList::CopyBufferTextureImpl(bool buffer_src,
                                          const std::shared_ptr<Resource>& buffer,
                                          const std::shared_ptr<Resource>& texture,
                                          const std::vector<BufferTextureCopyRegion>& regions)
{
    OpenComputeEncoder();
    decltype(auto) mt_buffer = CastToImpl<MTResource>(buffer);
    decltype(auto) mt_texture = CastToImpl<MTResource>(texture);
    AddAllocation(mt_buffer->GetBuffer());
    AddAllocation(mt_texture->GetTexture());
    auto format = texture->GetFormat();
    AddComputeBarriers();
    for (const auto& region : regions) {
        uint32_t bytes_per_image = 0;
        if (gli::is_compressed(format)) {
            auto extent = gli::block_extent(format);
            bytes_per_image =
                region.buffer_row_pitch *
                ((region.texture_extent.height + gli::block_extent(format).y - 1) / gli::block_extent(format).y);
        } else {
            bytes_per_image = region.buffer_row_pitch * region.texture_extent.height;
        }
        MTLOrigin region_origin = { region.texture_offset.x, region.texture_offset.y, region.texture_offset.z };
        MTLSize region_size = { region.texture_extent.width, region.texture_extent.height,
                                region.texture_extent.depth };
        if (buffer_src) {
            [state_->compute_encoder copyFromBuffer:mt_buffer->GetBuffer()
                                       sourceOffset:region.buffer_offset
                                  sourceBytesPerRow:region.buffer_row_pitch
                                sourceBytesPerImage:bytes_per_image
                                         sourceSize:region_size
                                          toTexture:mt_texture->GetTexture()
                                   destinationSlice:region.texture_array_layer
                                   destinationLevel:region.texture_mip_level
                                  destinationOrigin:region_origin];
        } else {
            [state_->compute_encoder copyFromTexture:mt_texture->GetTexture()
                                         sourceSlice:region.texture_array_layer
                                         sourceLevel:region.texture_mip_level
                                        sourceOrigin:region_origin
                                          sourceSize:region_size
                                            toBuffer:mt_buffer->GetBuffer()
                                   destinationOffset:region.buffer_offset
                              destinationBytesPerRow:region.buffer_row_pitch
                            destinationBytesPerImage:bytes_per_image];
        }
    }
}

void MTCommandList::CopyTexture(const std::shared_ptr<Resource>& src_texture,
                                const std::shared_ptr<Resource>& dst_texture,
                                const std::vector<TextureCopyRegion>& regions)
{
    OpenComputeEncoder();
    decltype(auto) mt_src_texture = CastToImpl<MTResource>(src_texture);
    decltype(auto) mt_dst_texture = CastToImpl<MTResource>(dst_texture);
    AddAllocation(mt_src_texture->GetTexture());
    AddAllocation(mt_dst_texture->GetTexture());
    auto format = dst_texture->GetFormat();
    AddComputeBarriers();
    for (const auto& region : regions) {
        MTLOrigin src_origin = { region.src_offset.x, region.src_offset.y, region.src_offset.z };
        MTLOrigin dst_origin = { region.dst_offset.x, region.dst_offset.y, region.dst_offset.z };
        MTLSize region_size = { region.extent.width, region.extent.height, region.extent.depth };
        [state_->compute_encoder copyFromTexture:mt_src_texture->GetTexture()
                                     sourceSlice:region.src_array_layer
                                     sourceLevel:region.src_mip_level
                                    sourceOrigin:src_origin
                                      sourceSize:region_size
                                       toTexture:mt_dst_texture->GetTexture()
                                destinationSlice:region.dst_array_layer
                                destinationLevel:region.dst_mip_level
                               destinationOrigin:dst_origin];
    }
}

void MTCommandList::WriteAccelerationStructuresProperties(
    const std::vector<std::shared_ptr<Resource>>& acceleration_structures,
    const std::shared_ptr<QueryHeap>& query_heap,
    uint32_t first_query)
{
    OpenComputeEncoder();
    AddComputeBarriers();
    id<MTLBuffer> mt_query_buffer = CastToImpl<MTQueryHeap>(query_heap)->GetBuffer();
    AddAllocation(mt_query_buffer);
    for (size_t i = 0; i < acceleration_structures.size(); ++i) {
        id<MTLAccelerationStructure> mt_acceleration_structure =
            CastToImpl<MTResource>(acceleration_structures[i])->GetAccelerationStructure();
        AddAllocation(mt_acceleration_structure);
        [state_->compute_encoder
            writeCompactedAccelerationStructureSize:mt_acceleration_structure
                                           toBuffer:MTL4BufferRangeMake(mt_query_buffer.gpuAddress +
                                                                            (first_query + i) * sizeof(uint64_t),
                                                                        sizeof(uint64_t))];
    }
}

void MTCommandList::ResolveQueryData(const std::shared_ptr<QueryHeap>& query_heap,
                                     uint32_t first_query,
                                     uint32_t query_count,
                                     const std::shared_ptr<Resource>& dst_buffer,
                                     uint64_t dst_offset)
{
    if (state_->compute_encoder) {
        [state_->compute_encoder barrierAfterEncoderStages:MTLStageAccelerationStructure
                                       beforeEncoderStages:MTLStageBlit
                                         visibilityOptions:MTL4VisibilityOptionNone];
    } else {
        OpenComputeEncoder();
    }
    AddComputeBarriers();
    id<MTLBuffer> mt_query_buffer = CastToImpl<MTQueryHeap>(query_heap)->GetBuffer();
    id<MTLBuffer> mt_dst_buffer = CastToImpl<MTResource>(dst_buffer)->GetBuffer();
    AddAllocation(mt_query_buffer);
    AddAllocation(mt_dst_buffer);
    [state_->compute_encoder copyFromBuffer:mt_query_buffer
                               sourceOffset:first_query * sizeof(uint64_t)
                                   toBuffer:mt_dst_buffer
                          destinationOffset:dst_offset
                                       size:sizeof(uint64_t) * query_count];
}

void MTCommandList::SetName(const std::string& name)
{
    command_buffer_.label = [NSString stringWithUTF8String:name.c_str()];
}

id<MTL4CommandBuffer> MTCommandList::GetCommandBuffer()
{
    return command_buffer_;
}

void MTCommandList::ApplyComputeState()
{
    if (state_->need_apply_binding_set && state_->binding_set) {
        state_->binding_set->Apply(state_->argument_tables, state_->pipeline, state_->residency_set);
        state_->need_apply_binding_set = false;
    }

    if (state_->need_apply_pipeline) {
        assert(state_->pipeline->GetPipelineType() == PipelineType::kCompute);
        decltype(auto) mt_pipeline = CastToImpl<MTComputePipeline>(state_->pipeline);
        [state_->compute_encoder setComputePipelineState:mt_pipeline->GetPipeline()];
    }
}

void MTCommandList::ApplyGraphicsState()
{
    if (state_->need_apply_binding_set && state_->binding_set) {
        state_->binding_set->Apply(state_->argument_tables, state_->pipeline, state_->residency_set);
        state_->need_apply_binding_set = false;
    }

    if (state_->need_apply_pipeline) {
        assert(state_->pipeline->GetPipelineType() == PipelineType::kGraphics);
        decltype(auto) mt_pipeline = CastToImpl<MTGraphicsPipeline>(state_->pipeline);
        decltype(auto) rasterizer_desc = mt_pipeline->GetDesc().rasterizer_desc;
        [state_->render_encoder setRenderPipelineState:mt_pipeline->GetPipeline()];
        [state_->render_encoder setTriangleFillMode:ConvertFillMode(rasterizer_desc.fill_mode)];
        [state_->render_encoder setCullMode:ConvertCullMode(rasterizer_desc.cull_mode)];
        [state_->render_encoder setFrontFacingWinding:ConvertFrontFace(rasterizer_desc.front_face)];
        [state_->render_encoder setDepthBias:rasterizer_desc.depth_bias
                                  slopeScale:rasterizer_desc.slope_scaled_depth_bias
                                       clamp:rasterizer_desc.depth_bias_clamp];
        [state_->render_encoder
            setDepthClipMode:rasterizer_desc.depth_clip_enable ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
        [state_->render_encoder setDepthStencilState:mt_pipeline->GetDepthStencil()];
        state_->need_apply_pipeline = false;
    }

    AddGraphicsBarriers();
}

void MTCommandList::AddGraphicsBarriers()
{
    if (state_->render_barrier_after_stages && state_->render_barrier_before_stages) {
        [state_->render_encoder barrierAfterQueueStages:state_->render_barrier_after_stages
                                           beforeStages:state_->render_barrier_before_stages
                                      visibilityOptions:MTL4VisibilityOptionNone];
        state_->render_barrier_after_stages = 0;
        state_->render_barrier_before_stages = 0;
    }
}

void MTCommandList::AddComputeBarriers()
{
    if (state_->compute_barrier_after_stages && state_->compute_barrier_before_stages) {
        [state_->compute_encoder barrierAfterQueueStages:state_->compute_barrier_after_stages
                                            beforeStages:state_->compute_barrier_before_stages
                                       visibilityOptions:MTL4VisibilityOptionNone];
        state_->compute_barrier_after_stages = 0;
        state_->compute_barrier_before_stages = 0;
    }
}

void MTCommandList::CreateArgumentTables()
{
    state_->argument_tables[ShaderType::kVertex] = CreateArgumentTable(device_);
    state_->argument_tables[ShaderType::kPixel] = CreateArgumentTable(device_);
    state_->argument_tables[ShaderType::kAmplification] = CreateArgumentTable(device_);
    state_->argument_tables[ShaderType::kMesh] = CreateArgumentTable(device_);
    state_->argument_tables[ShaderType::kCompute] = CreateArgumentTable(device_);
}

void MTCommandList::AddAllocation(id<MTLAllocation> allocation)
{
    [state_->residency_set addAllocation:allocation];
}

void MTCommandList::OpenComputeEncoder()
{
    if (state_->compute_encoder) {
        return;
    }

    assert(!state_->render_encoder);
    state_->compute_encoder = [command_buffer_ computeCommandEncoder];
    [state_->compute_encoder setArgumentTable:state_->argument_tables.at(ShaderType::kCompute)];
    state_->need_apply_pipeline = true;
}

void MTCommandList::CloseComputeEncoder()
{
    if (!state_->compute_encoder) {
        return;
    }

    [state_->compute_encoder endEncoding];
    state_->compute_encoder = nullptr;
}
