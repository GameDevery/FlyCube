#include "Shader/MTShader.h"

#include "Device/MTDevice.h"
#include "Utilities/Logging.h"

#if defined(USE_METAL_SHADER_CONVERTER)
#include "HLSLCompiler/MetalShaderConverter.h"
#else
#include "HLSLCompiler/MSLConverter.h"
#endif

MTShader::MTShader(MTDevice& device, const std::vector<uint8_t>& blob, ShaderBlobType blob_type, ShaderType shader_type)
    : ShaderBase(blob, blob_type, shader_type)
{
#if defined(USE_METAL_SHADER_CONVERTER)
    std::string entry_point;
    auto metal_lib_bytecode = ConvertToMetalLibBytecode(shader_type, blob, entry_point, binding_offsets_);
    dispatch_data_t metal_lib_data = dispatch_data_create(metal_lib_bytecode.data(), metal_lib_bytecode.size(), nullptr,
                                                          DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError* error = nullptr;
    id<MTLLibrary> library = [device.GetDevice() newLibraryWithData:metal_lib_data error:&error];
    if (library == nullptr) {
        Logging::Println("Failed to create MTLLibrary: {}", error);
    }
#else
    std::string entry_point;
    std::string msl_source = GetMSLShader(shader_type, blob_, slot_remapping_, entry_point);

    MTL4LibraryDescriptor* library_descriptor = [MTL4LibraryDescriptor new];
    library_descriptor.source = [NSString stringWithUTF8String:msl_source.c_str()];
    NSError* error = nullptr;
    id<MTLLibrary> library = [device.GetCompiler() newLibraryWithDescriptor:library_descriptor error:&error];
    if (!library) {
        Logging::Println("Failed to create MTLLibrary: {}", error);
    }
#endif

    function_descriptor_ = [MTL4LibraryFunctionDescriptor new];
    function_descriptor_.library = library;
    function_descriptor_.name = [NSString stringWithUTF8String:entry_point.c_str()];
}

#if defined(USE_METAL_SHADER_CONVERTER)
uint32_t MTShader::GetBindingOffset(const std::pair<uint32_t, uint32_t>& slot_space) const
{
    return binding_offsets_.at(slot_space);
}
#else
uint32_t MTShader::GetIndex(BindKey bind_key) const
{
    return slot_remapping_.at(bind_key);
}
#endif

MTL4LibraryFunctionDescriptor* MTShader::GetFunctionDescriptor()
{
    return function_descriptor_;
}
