#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include "editor/JsonParser.hpp"
#include "editor/import/BaseTypedImporter.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"
#include "engine/graphics/ShaderBinary.hpp"

namespace tryeditor {

struct SlangImportSettings {};

class SlangImporter : public IAssetImporter {
public:
    [[nodiscard]] constexpr eastl::string_view GetName() const override { return importer_name_; }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override {
        ImportResult result{};

        if (ctx.source_bytes.empty())
            co_return LogAndMakeError("Slang source is empty");

        AssetMeta<SlangImportSettings> meta;
        if (!ctx.meta_bytes.empty()) {
            auto parsed_meta = Deserialize<AssetMeta<SlangImportSettings>>(ctx.meta_bytes);
            meta = parsed_meta.value_or(GenerateDefaultMeta());
        } else {
            meta = GenerateDefaultMeta();
        }

        eastl::string source_code(reinterpret_cast<const char*>(ctx.source_bytes.data()), ctx.source_bytes.size());

        Slang::ComPtr<slang::IGlobalSession> global_session;
        if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
            co_return LogAndMakeError("Slang: Failed to create global session");
        }

        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = global_session->findProfile("spirv_1_5");

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target_desc;
        session_desc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            co_return LogAndMakeError("Slang: Failed to create session");
        }

        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* module =
            session->loadModuleFromSourceString("imported_shader", "shader.slang", source_code.c_str(), diagnostics.writeRef());

        if (!module) {
            const char* error_msg = diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Unknown error";
            co_return LogAndMakeError("Slang compilation error:\n{}", error_msg);
        }

        Slang::ComPtr<slang::IEntryPoint> vs_entry, fs_entry;
        module->findEntryPointByName("vsMain", vs_entry.writeRef());
        module->findEntryPointByName("fsMain", fs_entry.writeRef());

        if (!vs_entry || !fs_entry) {
            module->findEntryPointByName("main", vs_entry.writeRef());
            module->findEntryPointByName("main", fs_entry.writeRef());
        }

        if (!vs_entry || !fs_entry) {
            co_return LogAndMakeError("Slang: Entry points vsMain/fsMain not found");
        }

        slang::IComponentType* vs_components[] = {module, vs_entry.get()};
        Slang::ComPtr<slang::IComponentType> vs_program;
        session->createCompositeComponentType(vs_components, 2, vs_program.writeRef(), diagnostics.writeRef());

        slang::IComponentType* fs_components[] = {module, fs_entry.get()};
        Slang::ComPtr<slang::IComponentType> fs_program;
        session->createCompositeComponentType(fs_components, 2, fs_program.writeRef(), diagnostics.writeRef());

        slang::IComponentType* full_components[] = {module, vs_entry.get(), fs_entry.get()};
        Slang::ComPtr<slang::IComponentType> full_program;
        session->createCompositeComponentType(full_components, 3, full_program.writeRef(), diagnostics.writeRef());

        Slang::ComPtr<slang::IBlob> vs_spirv, fs_spirv;
        vs_program->getEntryPointCode(0, 0, vs_spirv.writeRef(), diagnostics.writeRef());
        fs_program->getEntryPointCode(0, 0, fs_spirv.writeRef(), diagnostics.writeRef());

        tryengine::graphics::ShaderBinaryContent binary_content{};

        // Извлекаем точные счетчики ресурсов для каждой стадии
        CountStageResources(vs_program->getLayout(), binary_content.vertex_counts);
        CountStageResources(fs_program->getLayout(), binary_content.fragment_counts);

        // Извлекаем объединенную рефлексию всей программы
        ExtractReflection(full_program->getLayout(), binary_content.reflection);

        binary_content.vertex_spv.assign(
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()) + vs_spirv->getBufferSize());

        binary_content.fragment_spv.assign(
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()) + fs_spirv->getBufferSize());

        eastl::vector<uint8_t> packed_binary = tryengine::graphics::PackShaderBinary(binary_content);

        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".tshd";
        artifact.bytes = std::move(packed_binary);

        result.artifacts.push_back(std::move(artifact));
        result.main_guid = meta.header.guid;

        co_return result;
    }

private:
    tryengine::graphics::ShaderParamType MapSlangType(slang::TypeReflection* type) const {
        using namespace tryengine::graphics;
        slang::TypeReflection::Kind kind = type->getKind();

        if (kind == slang::TypeReflection::Kind::Scalar) {
            slang::TypeReflection::ScalarType scalar = type->getScalarType();

            if (scalar == slang::TypeReflection::ScalarType::Int32 ||
                scalar == slang::TypeReflection::ScalarType::UInt32 ||
                scalar == slang::TypeReflection::ScalarType::Bool)
            {
                return ShaderParamType::Int;
            }
            return ShaderParamType::Float;
        }
        if (kind == slang::TypeReflection::Kind::Vector) {
            uint32_t elem_count = type->getElementCount();
            if (elem_count == 2) return ShaderParamType::Vec2;
            if (elem_count == 3) return ShaderParamType::Vec3;
            if (elem_count == 4) return ShaderParamType::Vec4;
        }
        if (kind == slang::TypeReflection::Kind::Matrix) {
            uint32_t rows = type->getRowCount();
            if (rows == 3) return ShaderParamType::Mat3;
            if (rows == 4) return ShaderParamType::Mat4;
        }
        return ShaderParamType::Float;
    }

    void CountStageResources(slang::ProgramLayout* layout, tryengine::graphics::StageResourceCounts& counts) const {
        if (!layout) return;
        uint32_t param_count = layout->getParameterCount();
        for (uint32_t i = 0; i < param_count; ++i) {
            slang::VariableLayoutReflection* var = layout->getParameterByIndex(i);
            slang::TypeLayoutReflection* type_layout = var->getTypeLayout();
            slang::TypeReflection::Kind kind = type_layout->getType()->getKind();

            if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock) {
                counts.num_uniform_buffers++;
            } else if (kind == slang::TypeReflection::Kind::Resource) {
                counts.num_samplers++;
            } else if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer) {
                counts.num_storage_buffers++;
            }
        }
    }

    void ExtractReflection(slang::ProgramLayout* layout, tryengine::graphics::ShaderReflectionData& out_reflection) const {
        if (!layout) return;

        uint32_t param_count = layout->getParameterCount();
        for (uint32_t i = 0; i < param_count; ++i) {
            slang::VariableLayoutReflection* var = layout->getParameterByIndex(i);
            slang::TypeLayoutReflection* type_layout = var->getTypeLayout();
            slang::TypeReflection* type = type_layout->getType();

            tryengine::graphics::ShaderReflectedBinding binding{};
            binding.name = var->getName() ? var->getName() : "";
            binding.name_hash = tryengine::graphics::RGTagOf(binding.name);
            binding.binding = var->getBindingIndex();
            binding.set = var->getBindingSpace();
            binding.size = static_cast<uint32_t>(type_layout->getSize());

            slang::TypeReflection::Kind kind = type->getKind();
            if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock) {
                binding.kind = tryengine::graphics::ShaderResourceKind::UniformBuffer;

                // Извлечение полей структуры внутри UBO
                slang::TypeLayoutReflection* element_layout = type_layout->getElementTypeLayout();
                if (element_layout) {
                    uint32_t field_count = element_layout->getFieldCount();
                    for (uint32_t f = 0; f < field_count; ++f) {
                        slang::VariableLayoutReflection* field_var = element_layout->getFieldByIndex(f);
                        slang::TypeLayoutReflection* field_type_layout = field_var->getTypeLayout();

                        tryengine::graphics::ShaderReflectedParam param{};
                        param.name = field_var->getName() ? field_var->getName() : "";
                        param.name_hash = tryengine::graphics::RGTagOf(param.name);
                        param.offset = static_cast<uint32_t>(field_var->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
                        param.size = static_cast<uint32_t>(field_type_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
                        param.type = MapSlangType(field_type_layout->getType());

                        binding.params.push_back(std::move(param));
                    }
                }
            } else if (kind == slang::TypeReflection::Kind::Resource) {
                binding.kind = tryengine::graphics::ShaderResourceKind::SampledTexture;
            } else if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer) {
                binding.kind = tryengine::graphics::ShaderResourceKind::StorageBufferRead;
            }

            out_reflection.bindings.push_back(std::move(binding));
        }
    }

    AssetMeta<SlangImportSettings> GenerateDefaultMeta() const {
        AssetMeta<SlangImportSettings> result;
        result.header.importer_type = GetName().data();
        result.header.guid = tryengine::core::random::GenerateInt64();
        return result;
    }

    const char* importer_name_ = "SlangImporter";
};

}  // namespace tryeditor