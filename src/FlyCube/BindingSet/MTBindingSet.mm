#include "BindingSet/MTBindingSet.h"

#include "BindingSetLayout/MTBindingSetLayout.h"
#include "Device/MTDevice.h"
#include "Pipeline/MTPipeline.h"
#include "Shader/MTShader.h"
#include "Utilities/Cast.h"
#include "Utilities/Check.h"
#include "Utilities/NotReached.h"
#include "View/MTView.h"

MTBindingSet::MTBindingSet(MTDevice& device, const std::shared_ptr<MTBindingSetLayout>& layout)
    : device_(device)
{
    for (const auto& bind_key : layout->GetBindKeys()) {
        if (bind_key.count == kBindlessCount) {
            bindless_bind_keys_.insert(bind_key);
        }
    }

    CreateConstantsFallbackBuffer(device_, layout->GetConstants());
    for (const auto& [bind_key, view] : fallback_constants_buffer_views_) {
        direct_bindings_.insert_or_assign(bind_key, view);
    }
}

void MTBindingSet::WriteBindings(const WriteBindingsDesc& desc)
{
    for (const auto& [bind_key, view] : desc.bindings) {
        assert(bind_key.count != kBindlessCount);
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
}
