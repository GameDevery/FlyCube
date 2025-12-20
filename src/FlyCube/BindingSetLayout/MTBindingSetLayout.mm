#include "BindingSetLayout/MTBindingSetLayout.h"

#include "Device/MTDevice.h"
#include "Utilities/Check.h"

#if defined(USE_METAL_SHADER_CONVERTER)
#include "HLSLCompiler/MetalShaderConverter.h"

#include <metal_irconverter_runtime.h>
#endif

MTBindingSetLayout::MTBindingSetLayout(MTDevice& device, const BindingSetLayoutDesc& desc)
    : device_(device)
    , bind_keys_(desc.bind_keys)
    , constants_(desc.constants)
{
#if defined(USE_METAL_SHADER_CONVERTER)
    uint32_t spaces = 0;
    std::map<uint32_t, uint32_t> slots;
    for (const auto& bind_key : bind_keys_) {
        spaces = std::max(spaces, bind_key.space + 1);
        if (bind_key.count != kBindlessCount) {
            auto& count = slots[GetArgumentBufferKey(bind_key.space, bind_key.view_type)];
            count = std::max(count, bind_key.slot + 1);
        }
    }
    for (const auto& [bind_key, _] : constants_) {
        DCHECK(bind_key.count == 1);
        spaces = std::max(spaces, bind_key.space + 1);
        auto& count = slots[GetArgumentBufferKey(bind_key.space, bind_key.view_type)];
        count = std::max(count, bind_key.slot + 1);
    }
    argument_buffer_size_ = spaces * kDxilMaxRangeType * sizeof(uint64_t);
    for (const auto& [argument_buffer_key, count] : slots) {
        argument_buffer_offsets_[argument_buffer_key] = argument_buffer_size_;
        argument_buffer_size_ += count * sizeof(IRDescriptorTableEntry);
    }
#endif
}

const std::vector<BindKey>& MTBindingSetLayout::GetBindKeys() const
{
    return bind_keys_;
}

const std::vector<BindingConstants>& MTBindingSetLayout::GetConstants() const
{
    return constants_;
}

#if defined(USE_METAL_SHADER_CONVERTER)
uint64_t MTBindingSetLayout::GetArgumentBufferSize() const
{
    return argument_buffer_size_;
}

const std::map<uint32_t, uint64_t>& MTBindingSetLayout::GetArgumentBufferOffsets() const
{
    return argument_buffer_offsets_;
}
#endif
