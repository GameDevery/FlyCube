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
    uint32_t spaces = 0;
    std::map<uint32_t, uint32_t> slots;
    for (const auto& bind_key : layout->GetBindKeys()) {
        spaces = std::max(spaces, bind_key.space + 1);
        if (bind_key.count != kBindlessCount) {
            slots[bind_key.space] = std::max(slots[bind_key.space], bind_key.slot + 1);
        }
    }
    for (const auto& [bind_key, _] : layout->GetConstants()) {
        DCHECK(bind_key.count == 1);
        spaces = std::max(spaces, bind_key.space + 1);
        slots[bind_key.space] = std::max(slots[bind_key.space], bind_key.slot + 1);
    }

    if (spaces > 0) {
        argument_buffer_ = [device_.GetDevice() newBufferWithLength:spaces * sizeof(uint64_t)
                                                            options:MTLResourceStorageModeShared];
        uint64_t* argument_buffer_data = reinterpret_cast<uint64_t*>(argument_buffer_.contents);
        for (size_t i = 0; i < spaces; ++i) {
            if (slots[i] == 0) {
                continue;
            }
            bindings_by_space_[i] = [device_.GetDevice() newBufferWithLength:slots[i] * sizeof(IRDescriptorTableEntry)
                                                                     options:MTLResourceStorageModeShared];
            argument_buffer_data[i] = bindings_by_space_[i].gpuAddress;
        }

        id<MTLBuffer> buffer = device_.GetBindlessArgumentBuffer().GetArgumentBuffer();
        for (const auto& bind_key : bindless_bind_keys_) {
            argument_buffer_data[bind_key.space] = buffer.gpuAddress;
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
    for (const auto& [bind_key, view] : desc.bindings) {
        IRDescriptorTableEntry* entries =
            static_cast<IRDescriptorTableEntry*>(bindings_by_space_[bind_key.space].contents);
        auto* mt_view = CastToImpl<MTView>(view);
        mt_view->BindView(&entries[bind_key.slot]);
    }
#endif

    for (const auto& [bind_key, view] : desc.bindings) {
        DCHECK(bind_key.count != kBindlessCount);
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
#if defined(USE_METAL_SHADER_CONVERTER)
    for (const auto& [bind_key, view] : direct_bindings_) {
        auto* mt_view = CastToImpl<MTView>(view);
        id<MTLResource> allocation = mt_view->GetAllocation();
        if (allocation) {
            [residency_set addAllocation:allocation];
        }

        decltype(auto) argument_table = argument_tables.at(bind_key.shader_type);
        [argument_table setAddress:argument_buffer_.gpuAddress atIndex:kIRArgumentBufferBindPoint];
        [residency_set addAllocation:argument_buffer_];
        [residency_set addAllocation:bindings_by_space_[bind_key.space]];
    }

    if (!bindless_bind_keys_.empty()) {
        id<MTLBuffer> buffer = device_.GetBindlessArgumentBuffer().GetArgumentBuffer();
        [residency_set addAllocation:buffer];
    }
#else
    for (const auto& [bind_key, view] : direct_bindings_) {
        decltype(auto) shader = CastToImpl<MTPipeline>(pipeline)->GetShader(bind_key.shader_type);
        uint32_t index = CastToImpl<MTShader>(shader)->GetIndex(bind_key);
        auto* mt_view = CastToImpl<MTView>(view);
        DCHECK(mt_view);
        decltype(auto) argument_table = argument_tables.at(bind_key.shader_type);
        mt_view->BindView(argument_table, index);
        id<MTLResource> allocation = mt_view->GetAllocation();
        if (allocation) {
            [residency_set addAllocation:allocation];
        }
    }

    if (bindless_bind_keys_.empty()) {
        return;
    }

    id<MTLBuffer> buffer = device_.GetBindlessArgumentBuffer().GetArgumentBuffer();
    for (const auto& bind_key : bindless_bind_keys_) {
        decltype(auto) shader = CastToImpl<MTPipeline>(pipeline)->GetShader(bind_key.shader_type);
        uint32_t index = CastToImpl<MTShader>(shader)->GetIndex(bind_key);
        decltype(auto) argument_table = argument_tables.at(bind_key.shader_type);
        [argument_table setAddress:buffer.gpuAddress atIndex:index];
    }
    [residency_set addAllocation:buffer];
#endif
}
