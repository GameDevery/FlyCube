#pragma once
#include "Instance/BaseTypes.h"

inline constexpr uint32_t kDxilMaxRangeType = 4;

uint32_t GetRangeType(ViewType view_type);

inline uint32_t GetArgumentBufferKey(uint32_t space, ViewType view_type)
{
    return space * kDxilMaxRangeType + GetRangeType(view_type);
}

std::vector<uint8_t> ConvertToMetalLibBytecode(ShaderType shader_type,
                                               const std::vector<uint8_t>& blob,
                                               std::string& entry_point);
