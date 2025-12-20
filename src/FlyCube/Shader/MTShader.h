#pragma once
#include "Instance/BaseTypes.h"
#include "Shader/ShaderBase.h"

#import <Metal/Metal.h>

#include <map>
#include <vector>

class MTDevice;

class MTShader : public ShaderBase {
public:
    MTShader(MTDevice& device, const std::vector<uint8_t>& blob, ShaderBlobType blob_type, ShaderType shader_type);

#if defined(USE_METAL_SHADER_CONVERTER)
    uint32_t GetBindingOffset(const std::pair<uint32_t, uint32_t>& slot_space) const;
#else
    uint32_t GetIndex(BindKey bind_key) const;
#endif

    MTL4LibraryFunctionDescriptor* GetFunctionDescriptor();

private:
    MTL4LibraryFunctionDescriptor* function_descriptor_ = nullptr;
#if defined(USE_METAL_SHADER_CONVERTER)
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> binding_offsets_;
#else
    std::map<BindKey, uint32_t> slot_remapping_;
#endif
};
