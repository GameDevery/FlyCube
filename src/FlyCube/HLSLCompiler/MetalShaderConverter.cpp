#include "HLSLCompiler/MetalShaderConverter.h"

#include "Utilities/Logging.h"
#include "Utilities/NotReached.h"

#include <metal_irconverter.h>

namespace {

IRShaderStage GetShaderStage(ShaderType type)
{
    switch (type) {
    case ShaderType::kVertex:
        return IRShaderStageVertex;
    case ShaderType::kPixel:
        return IRShaderStageFragment;
    case ShaderType::kGeometry:
        return IRShaderStageGeometry;
    case ShaderType::kCompute:
        return IRShaderStageCompute;
    case ShaderType::kAmplification:
        return IRShaderStageAmplification;
    case ShaderType::kMesh:
        return IRShaderStageMesh;
    default:
        NOTREACHED();
    }
}

} // namespace

std::vector<uint8_t> ConvertToMetalLibBytecode(ShaderType shader_type,
                                               const std::vector<uint8_t>& blob,
                                               std::string& entry_point,
                                               std::map<std::pair<uint32_t, uint32_t>, uint32_t>& binding_offsets)
{
    IRCompiler* compiler = IRCompilerCreate();
    IRObject* dxil_obj = IRObjectCreateFromDXIL(blob.data(), blob.size(), IRBytecodeOwnershipNone);

    if (shader_type == ShaderType::kVertex) {
        IRCompilerSetStageInGenerationMode(compiler, IRStageInCodeGenerationModeUseMetalVertexFetch);
    }

    IRError* error = nullptr;
    IRObject* metal_ir = IRCompilerAllocCompileAndLink(compiler, nullptr, dxil_obj, &error);
    if (!metal_ir) {
        Logging::Println("IRCompilerAllocCompileAndLink failed: {}", IRErrorGetCode(error));
        IRErrorDestroy(error);
        IRObjectDestroy(dxil_obj);
        IRCompilerDestroy(compiler);
        return {};
    }

    auto* metal_lib = IRMetalLibBinaryCreate();
    IRObjectGetMetalLibBinary(metal_ir, GetShaderStage(shader_type), metal_lib);

    size_t metal_lib_size = IRMetalLibGetBytecodeSize(metal_lib);
    std::vector<uint8_t> metal_lib_bytecode(metal_lib_size);
    IRMetalLibGetBytecode(metal_lib, metal_lib_bytecode.data());

    IRShaderReflection* reflection = IRShaderReflectionCreate();
    IRObjectGetReflection(metal_ir, GetShaderStage(shader_type), reflection);
    entry_point = IRShaderReflectionGetEntryPointFunctionName(reflection);

    size_t resource_count = IRShaderReflectionGetResourceCount(reflection);
    std::vector<IRResourceLocation> resources(resource_count);
    IRShaderReflectionGetResourceLocations(reflection, resources.data());
    for (const auto& resource : resources) {
        binding_offsets[{ resource.slot, resource.space }] = resource.topLevelOffset;
        assert(resource.sizeBytes == 3 * sizeof(uint64_t));
    }

    IRShaderReflectionDestroy(reflection);
    IRMetalLibBinaryDestroy(metal_lib);
    IRObjectDestroy(metal_ir);
    IRObjectDestroy(dxil_obj);
    IRCompilerDestroy(compiler);

    return metal_lib_bytecode;
}
