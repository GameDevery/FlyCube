#include "HLSLCompiler/MetalShaderConverter.h"

#include "ShaderReflection/ShaderReflection.h"
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

IRDescriptorRangeType GetRangeType(ViewType view_type)
{
    switch (view_type) {
    case ViewType::kTexture:
    case ViewType::kBuffer:
    case ViewType::kStructuredBuffer:
    case ViewType::kByteAddressBuffer:
    case ViewType::kAccelerationStructure:
        return IRDescriptorRangeTypeSRV;
    case ViewType::kRWTexture:
    case ViewType::kRWBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kRWByteAddressBuffer:
        return IRDescriptorRangeTypeUAV;
    case ViewType::kConstantBuffer:
        return IRDescriptorRangeTypeCBV;
    case ViewType::kSampler:
        return IRDescriptorRangeTypeSampler;
    default:
        NOTREACHED();
    }
}

IRRootSignature* CreateIRRootSignature(const std::vector<BindKey>& bind_keys)
{
    uint32_t spaces = 0;
    for (const auto& bind_key : bind_keys) {
        spaces = std::max(spaces, bind_key.space + 1);
    }

    std::vector<std::vector<IRDescriptorRange1>> descriptor_table_ranges(spaces);
    for (const auto& bind_key : bind_keys) {
        auto& range = descriptor_table_ranges[bind_key.space].emplace_back();
        range.RangeType = GetRangeType(bind_key.view_type);
        range.NumDescriptors = bind_key.count;
        range.BaseShaderRegister = bind_key.slot;
        range.RegisterSpace = bind_key.space;
        range.Flags = IRDescriptorRangeFlagNone;
        range.OffsetInDescriptorsFromTableStart = bind_key.slot;
    }

    std::vector<IRRootParameter1> root_parameters;
    auto add_root_table = [&](size_t range_count, IRDescriptorRange1* ranges) {
        IRRootDescriptorTable1 descriptor_table = {};
        descriptor_table.NumDescriptorRanges = range_count;
        descriptor_table.pDescriptorRanges = ranges;

        IRRootParameter1& root_parameter = root_parameters.emplace_back();
        root_parameter.ParameterType = IRRootParameterTypeDescriptorTable;
        root_parameter.DescriptorTable = descriptor_table;
        root_parameter.ShaderVisibility = IRShaderVisibilityAll;
    };

    for (auto& descriptor_table_range : descriptor_table_ranges) {
        add_root_table(descriptor_table_range.size(), descriptor_table_range.data());
    }

    IRRootSignatureFlags root_signature_flags = static_cast<IRRootSignatureFlags>(
        IRRootSignatureFlagAllowInputAssemblerInputLayout | IRRootSignatureFlagDenyHullShaderRootAccess |
        IRRootSignatureFlagDenyDomainShaderRootAccess);

    IRVersionedRootSignatureDescriptor root_signature_desc = {};
    root_signature_desc.version = IRRootSignatureVersion_1_1;
    root_signature_desc.desc_1_1.Flags = root_signature_flags;
    root_signature_desc.desc_1_1.NumParameters = root_parameters.size();
    root_signature_desc.desc_1_1.pParameters = root_parameters.data();

    IRError* error = nullptr;
    IRRootSignature* root_signature = IRRootSignatureCreateFromDescriptor(&root_signature_desc, &error);
    if (!root_signature) {
        Logging::Println("IRRootSignatureCreateFromDescriptor failed: {}", IRErrorGetCode(error));
        IRErrorDestroy(error);
    }

    return root_signature;
}

} // namespace

std::vector<uint8_t> ConvertToMetalLibBytecode(ShaderType shader_type,
                                               const std::vector<uint8_t>& blob,
                                               std::string& entry_point)
{
    IRCompiler* compiler = IRCompilerCreate();
    IRObject* dxil_obj = IRObjectCreateFromDXIL(blob.data(), blob.size(), IRBytecodeOwnershipNone);

    if (shader_type == ShaderType::kVertex) {
        IRCompilerSetStageInGenerationMode(compiler, IRStageInCodeGenerationModeUseMetalVertexFetch);
    }

    std::vector<BindKey> bind_keys;
    auto dxil_reflection = CreateShaderReflection(ShaderBlobType::kDXIL, blob.data(), blob.size());
    for (const auto& binding : dxil_reflection->GetBindings()) {
        BindKey bind_key = {
            .shader_type = shader_type,
            .view_type = binding.type,
            .slot = binding.slot,
            .space = binding.space,
            .count = binding.count,
        };
        bind_keys.push_back(bind_key);
    }
    IRRootSignature* root_signature = CreateIRRootSignature(bind_keys);
    IRCompilerSetGlobalRootSignature(compiler, root_signature);

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

    IRShaderReflectionDestroy(reflection);
    IRMetalLibBinaryDestroy(metal_lib);
    IRRootSignatureDestroy(root_signature);
    IRObjectDestroy(metal_ir);
    IRObjectDestroy(dxil_obj);
    IRCompilerDestroy(compiler);

    return metal_lib_bytecode;
}
