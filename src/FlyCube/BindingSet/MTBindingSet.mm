#include "BindingSet/MTBindingSet.h"

#include "BindingSetLayout/MTBindingSetLayout.h"
#include "Device/MTDevice.h"
#include "Pipeline/MTPipeline.h"
#include "Shader/MTShader.h"
#include "Utilities/Cast.h"
#include "Utilities/Check.h"
#include "Utilities/NotReached.h"
#include "View/MTView.h"

#if defined(USE_METAL_SHADER_CONVERTER)
#include "HLSLCompiler/MetalShaderConverter.h"

#include <metal_irconverter_runtime.h>
#endif

MTBindingSet::MTBindingSet(MTDevice& device, const std::shared_ptr<MTBindingSetLayout>& layout)
    : device_(device)
{
    for (const auto& bind_key : layout->GetBindKeys()) {
        if (bind_key.count == kBindlessCount) {
            bindless_bind_keys_.insert(bind_key);
        }
    }

#if defined(USE_METAL_SHADER_CONVERTER)
    layout_ = layout;
    if (layout->GetArgumentBufferSize() > 0) {
        argument_buffer_ = [device_.GetDevice() newBufferWithLength:layout->GetArgumentBufferSize()
                                                            options:MTLResourceStorageModeShared];
        uint64_t* argument_buffer_data = static_cast<uint64_t*>(argument_buffer_.contents);
        for (const auto& [argument_buffer_key, offset] : layout->GetArgumentBufferOffsets()) {
            argument_buffer_data[argument_buffer_key] = argument_buffer_.gpuAddress + offset;
        }
        id<MTLBuffer> bindless_argument_buffer = device_.GetBindlessArgumentBuffer().GetArgumentBuffer();
        for (const auto& bind_key : bindless_bind_keys_) {
            const uint32_t argument_buffer_key = GetArgumentBufferKey(bind_key.space, bind_key.view_type);
            argument_buffer_data[argument_buffer_key] = bindless_argument_buffer.gpuAddress;
        }
    }
#endif

    CreateConstantsFallbackBuffer(device_, layout->GetConstants());
    std::vector<BindingDesc> fallback_constants_bindings;
    fallback_constants_bindings.reserve(fallback_constants_buffer_views_.size());
    for (const auto& [bind_key, view] : fallback_constants_buffer_views_) {
        fallback_constants_bindings.emplace_back(bind_key, view);
    }
    WriteBindings({ .bindings = fallback_constants_bindings });
}

void MTBindingSet::WriteBindings(const WriteBindingsDesc& desc)
{
#if defined(USE_METAL_SHADER_CONVERTER)
    uint8_t* argument_buffer_data = static_cast<uint8_t*>(argument_buffer_.contents);
    for (const auto& [bind_key, view] : desc.bindings) {
        DCHECK(view);
        const uint32_t argument_buffer_key = GetArgumentBufferKey(bind_key.space, bind_key.view_type);
        auto* entries = reinterpret_cast<IRDescriptorTableEntry*>(
            argument_buffer_data + layout_->GetArgumentBufferOffsets().at(argument_buffer_key));
        auto* mt_view = CastToImpl<MTView>(view);
        mt_view->BindView(&entries[bind_key.slot]);
    }
#endif

    for (const auto& [bind_key, view] : desc.bindings) {
        DCHECK(bind_key.count != kBindlessCount);
        DCHECK(view);
        direct_bindings_.insert_or_assign(bind_key, view);
    }
    for (const auto& [bind_key, data] : desc.constants) {
        fallback_constants_buffer_->UpdateUploadBuffer(fallback_constants_buffer_offsets_.at(bind_key), data.data(),
                                                       data.size());
    }
}

void MTBindingSet::Apply(const std::map<ShaderType, id<MTL4ArgumentTable>>& argument_tables,
                         const std::shared_ptr<Pipeline>& pipeline,
                         id<MTLResidencySet> residency_set)
{
    for (const auto& [bind_key, view] : direct_bindings_) {
        auto* mt_view = CastToImpl<MTView>(view);
        id<MTLResource> allocation = mt_view->GetAllocation();
        if (allocation) {
            [residency_set addAllocation:allocation];
        }
    }

    if (!bindless_bind_keys_.empty()) {
        [residency_set addAllocation:device_.GetBindlessArgumentBuffer().GetArgumentBuffer()];
    }

#if defined(USE_METAL_SHADER_CONVERTER)
    if (argument_buffer_) {
        for (const auto& [_, argument_table] : argument_tables) {
            [argument_table setAddress:argument_buffer_.gpuAddress atIndex:kIRArgumentBufferBindPoint];
        }
        [residency_set addAllocation:argument_buffer_];
    }
#else
    for (const auto& [bind_key, view] : direct_bindings_) {
        decltype(auto) shader = CastToImpl<MTPipeline>(pipeline)->GetShader(bind_key.shader_type);
        uint32_t index = CastToImpl<MTShader>(shader)->GetIndex(bind_key);
        decltype(auto) argument_table = argument_tables.at(bind_key.shader_type);
        CastToImpl<MTView>(view)->BindView(argument_table, index);
    }

    if (!bindless_bind_keys_.empty()) {
        id<MTLBuffer> bindless_argument_buffer = device_.GetBindlessArgumentBuffer().GetArgumentBuffer();
        for (const auto& bind_key : bindless_bind_keys_) {
            decltype(auto) shader = CastToImpl<MTPipeline>(pipeline)->GetShader(bind_key.shader_type);
            uint32_t index = CastToImpl<MTShader>(shader)->GetIndex(bind_key);
            decltype(auto) argument_table = argument_tables.at(bind_key.shader_type);
            [argument_table setAddress:bindless_argument_buffer.gpuAddress atIndex:index];
        }
    }
#endif
}
