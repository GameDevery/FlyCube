#pragma once
#include "Instance/BaseTypes.h"

std::vector<uint8_t> ConvertToMetalLibBytecode(ShaderType shader_type,
                                               const std::vector<uint8_t>& blob,
                                               std::string& entry_point,
                                               std::map<std::pair<uint32_t, uint32_t>, uint32_t>& binding_offsets);
