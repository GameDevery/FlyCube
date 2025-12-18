#pragma once
#include "Instance/BaseTypes.h"

std::vector<uint8_t> ConvertToMetalLibBytecode(ShaderType shader_type, const std::vector<uint8_t>& blob);
