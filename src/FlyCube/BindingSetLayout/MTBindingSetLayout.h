#pragma once
#include "BindingSetLayout/BindingSetLayout.h"

class MTDevice;

class MTBindingSetLayout : public BindingSetLayout {
public:
    MTBindingSetLayout(MTDevice& device, const BindingSetLayoutDesc& desc);

    const std::vector<BindKey>& GetBindKeys() const;
    const std::vector<BindingConstants>& GetConstants() const;
#if defined(USE_METAL_SHADER_CONVERTER)
    uint64_t GetArgumentBufferSize() const;
    const std::map<uint32_t, uint64_t>& GetArgumentBufferOffsets() const;
#endif

private:
    MTDevice& device_;
    std::vector<BindKey> bind_keys_;
    std::vector<BindingConstants> constants_;
#if defined(USE_METAL_SHADER_CONVERTER)
    uint64_t argument_buffer_size_ = 0;
    std::map<uint32_t, uint64_t> argument_buffer_offsets_;
#endif
};
