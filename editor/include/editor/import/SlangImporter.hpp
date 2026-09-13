#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <EASTL/hash_set.h>
#include <algorithm>
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

        // slang::CompilerOptionEntry option{};
        // option.name = slang::CompilerOptionName::VulkanSupportNonZeroBaseInstance;
        // option.value.kind = slang::CompilerOptionValueKind::Int;
        // option.value.intValue = 0; // false: отключает gl_BaseVertex / gl_BaseInstance


        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        // target_desc.profile = global_session->findProfile("spirv_1_5");
        target_desc.profile = global_session->findProfile("spirv_1_3");
        // target_desc.compilerOptionEntries = &option;

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

        if (diagnostics && diagnostics->getBufferSize() > 0) {
            // Раньше это логировалось только при провале компиляции (!module). Но компиляция МОЖЕТ успешно
            // завершиться и при этом вернуть diagnostics с предупреждениями — например, если [PerObj]/[Pass]/
            // [Material] не объявлены как пользовательские атрибуты через [__AttributeUsage(...)], Slang,
            // скорее всего, тихо (с warning'ом) их не регистрирует, и getUserAttributeByIndex() потом ничего
            // не находит. Логируем всегда, чтобы такие warning'и не терялись молча.
            LogError("Slang diagnostics (module compiled = {}):\n{}", module != nullptr,
                       static_cast<const char*>(diagnostics->getBufferPointer()));
        }

        if (!module) {
            const char* error_msg = diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Unknown error";
            co_return LogAndMakeError("Slang compilation error:\n{}", error_msg);
        }

        Slang::ComPtr<slang::IEntryPoint> vs_entry, fs_entry;
        module->findEntryPointByName("vsMain", vs_entry.writeRef());
        module->findEntryPointByName("fsMain", fs_entry.writeRef());

        if (!vs_entry || !fs_entry) {
            LogError("!vs_entry || !fs_entry");
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

        Slang::ComPtr<slang::IComponentType> vs_linked, fs_linked, full_linked;
        vs_program->link(vs_linked.writeRef(), diagnostics.writeRef());
        fs_program->link(fs_linked.writeRef(), diagnostics.writeRef());
        full_program->link(full_linked.writeRef(), diagnostics.writeRef());

        Slang::ComPtr<slang::IBlob> vs_spirv, fs_spirv;
        vs_linked->getEntryPointCode(0, 0, vs_spirv.writeRef(), diagnostics.writeRef());
        fs_linked->getEntryPointCode(0, 0, fs_spirv.writeRef(), diagnostics.writeRef());

        tryengine::graphics::ShaderBinaryContent binary_content{};

        binary_content.vertex_spv.assign(
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()) + vs_spirv->getBufferSize());

        binary_content.fragment_spv.assign(
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()) + fs_spirv->getBufferSize());

        auto vs_active_slots = ExtractActiveSpirvSlots(binary_content.vertex_spv);
        auto fs_active_slots = ExtractActiveSpirvSlots(binary_content.fragment_spv);

        auto vs_reflection = FilterReflectionByActiveSlots(
            ExtractReflection(vs_linked->getLayout(), tryengine::graphics::ShaderStage::Vertex), vs_active_slots);
        auto fs_reflection = FilterReflectionByActiveSlots(
            ExtractReflection(fs_linked->getLayout(), tryengine::graphics::ShaderStage::Fragment), fs_active_slots);

        // ВАЖНО: раньше здесь стояло `binary_content.reflection = ExtractReflection(full_linked->getLayout());`
        // Проблема в том, что ExtractReflection принимает ОДИН stage на весь набор биндингов, а у него
        // параметр по умолчанию — ShaderStage::Fragment. В итоге для full_linked (который содержит и
        // вершинные, и фрагментные ресурсы одновременно) абсолютно ВСЕ биндинги помечались как Fragment —
        // включая вершинные FrameUBO/PassUBO. Из-за этого BindPassResources/BindPerObjResources никогда не
        // находили Vertex-стадийные UBO и не пушили в вершинный шейдер camera.view/proj и model-матрицу.
        // binary_content.reflection — это именно то, что попадает в Shader::reflection и используется в
        // рантайме, поэтому баг был критичным. Фикс: используем уже честно застейдженные vs_reflection и
        // fs_reflection (полученные с явным stage-аргументом чуть выше) и просто объединяем их бindings.
        binary_content.reflection.bindings.clear();
        binary_content.reflection.bindings.insert(binary_content.reflection.bindings.end(),
                                                    vs_reflection.bindings.begin(), vs_reflection.bindings.end());
        binary_content.reflection.bindings.insert(binary_content.reflection.bindings.end(),
                                                    fs_reflection.bindings.begin(), fs_reflection.bindings.end());

        // full_linked/full_reflection по-прежнему считаем — исключительно для человекочитаемого дебаг-дампа
        // ниже (секция "FULL PROGRAM COMBINED"), на рантайм он больше не влияет.
        auto full_reflection_for_dump = ExtractReflection(full_linked->getLayout());

        // Вычисляем плотные 0-based слоты для SDL3 GPU привязок
        AssignSDL3Slots(vs_reflection);
        AssignSDL3Slots(fs_reflection);
        AssignSDL3Slots(binary_content.reflection);

        CountStageResourcesFromReflection(vs_reflection, binary_content.vertex_counts);
        CountStageResourcesFromReflection(fs_reflection, binary_content.fragment_counts);

        eastl::vector<uint8_t> packed_binary = tryengine::graphics::PackShaderBinary(binary_content);

        eastl::string debug_text = DumpReflectionToString(vs_reflection, fs_reflection, full_reflection_for_dump);
        ProducedArtifact debug_artifact{};
        debug_artifact.sub_guid = 88;
        debug_artifact.target = ArtifactTarget::Editor;
        debug_artifact.extension = ".reflection.txt";
        debug_artifact.bytes.assign(
            reinterpret_cast<const uint8_t*>(debug_text.data()),
            reinterpret_cast<const uint8_t*>(debug_text.data()) + debug_text.size()
        );
        result.artifacts.push_back(std::move(debug_artifact));

        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".tshd";
        artifact.bytes = std::move(packed_binary);

        result.artifacts.push_back(std::move(artifact));
        result.main_guid = meta.header.guid;

        auto meta_bytes = Serialize<AssetMeta<SlangImportSettings>>(meta);
        if (meta_bytes.has_value()) {
            result.meta_bytes = eastl::move(meta_bytes.value());
        } else {
            LogError("Что за хрень, не смогли сереализовать мету");
        }

        co_return result;
    }

private:
    void AssignSDL3Slots(tryengine::graphics::ShaderReflectionData& reflection) const {
        using namespace tryengine::graphics;

        std::sort(reflection.bindings.begin(), reflection.bindings.end(),
                  [](const ShaderReflectedBinding& a, const ShaderReflectedBinding& b) {
                      if (a.set != b.set) return a.set < b.set;
                      return a.binding < b.binding;
                  });

        uint32_t ubo_slot = 0;
        uint32_t sampler_slot = 0;
        uint32_t storage_tex_slot = 0;
        uint32_t storage_buf_slot = 0;

        for (auto& b : reflection.bindings) {
            switch (b.kind) {
                case ShaderResourceKind::UniformBuffer:
                    b.slot = ubo_slot++;
                    break;
                case ShaderResourceKind::SampledTexture:
                    b.slot = sampler_slot++;
                    break;
                case ShaderResourceKind::StorageTexture:
                    b.slot = storage_tex_slot++;
                    break;
                case ShaderResourceKind::StorageBufferRead:
                case ShaderResourceKind::StorageBufferWrite:
                    b.slot = storage_buf_slot++;
                    break;
            }
        }
    }

    static const char* KindToString(tryengine::graphics::ShaderResourceKind kind) {
        using namespace tryengine::graphics;
        switch (kind) {
            case ShaderResourceKind::UniformBuffer:      return "UniformBuffer";
            case ShaderResourceKind::SampledTexture:     return "SampledTexture";
            case ShaderResourceKind::StorageBufferRead:  return "StorageBufferRead";
            case ShaderResourceKind::StorageBufferWrite: return "StorageBufferWrite";
            case ShaderResourceKind::StorageTexture:     return "StorageTexture";
        }
        return "Unknown";
    }

    static const char* ScopeToString(tryengine::graphics::BindingScope scope) {
        using namespace tryengine::graphics;
        switch (scope) {
            case BindingScope::PerObj:   return "PerObj";
            case BindingScope::Material: return "Material";
            case BindingScope::Pass:     return "Pass";
        }
        return "Unknown";
    }

    static const char* ParamTypeToString(tryengine::graphics::ShaderParamType type) {
        using namespace tryengine::graphics;
        switch (type) {
            case ShaderParamType::Float: return "Float";
            case ShaderParamType::Int:   return "Int";
            case ShaderParamType::Vec2:  return "Vec2";
            case ShaderParamType::Vec3:  return "Vec3";
            case ShaderParamType::Vec4:  return "Vec4";
            case ShaderParamType::Mat3:  return "Mat3";
            case ShaderParamType::Mat4:  return "Mat4";
        }
        return "Unknown";
    }

    tryengine::graphics::BindingScope DetermineBindingScope(slang::VariableLayoutReflection* var_layout) const {
        using namespace tryengine::graphics;

        if (!var_layout) return BindingScope::Material;

        auto check_attributes = [](auto* reflection_object, BindingScope& out_scope) -> bool {
            if (!reflection_object) return false;

            uint32_t attr_count = reflection_object->getUserAttributeCount();
            for (uint32_t i = 0; i < attr_count; ++i) {
                slang::UserAttribute* attr = reflection_object->getUserAttributeByIndex(i);
                if (!attr) continue;

                const char* attr_name = attr->getName();
                if (!attr_name) continue;

                eastl::string_view name(attr_name);
                if (name == "PerObj") {
                    out_scope = BindingScope::PerObj;
                    return true;
                }
                if (name == "Pass") {
                    out_scope = BindingScope::Pass;
                    return true;
                }
                if (name == "Material") {
                    out_scope = BindingScope::Material;
                    return true;
                }
            }
            return false;
        };

        BindingScope scope = BindingScope::Material;

        if (check_attributes(var_layout->getVariable(), scope)) {
            return scope;
        }

        if (auto* type_layout = var_layout->getTypeLayout()) {
            if (check_attributes(type_layout->getType(), scope)) {
                return scope;
            }
        }

        return BindingScope::Material;
    }

    static constexpr uint64_t MakeSlotKey(uint32_t set, uint32_t binding) {
        return (static_cast<uint64_t>(set) << 32) | static_cast<uint64_t>(binding);
    }

    eastl::hash_set<uint64_t> ExtractActiveSpirvSlots(const eastl::vector<uint8_t>& spirv) const {
        eastl::hash_set<uint64_t> active_slots;
        if (spirv.size() < 20) return active_slots;

        const uint32_t* words = reinterpret_cast<const uint32_t*>(spirv.data());
        size_t word_count = spirv.size() / sizeof(uint32_t);

        if (words[0] != 0x07230203) return active_slots;

        struct IdDecorations {
            uint32_t set = 0;
            uint32_t binding = 0;
            bool has_set = false;
            bool has_binding = false;
        };
        eastl::hash_map<uint32_t, IdDecorations> decorations;

        size_t i = 5;
        while (i < word_count) {
            uint32_t instruction = words[i];
            uint16_t opcode = instruction & 0xFFFF;
            uint16_t length = instruction >> 16;

            if (length == 0 || i + length > word_count) break;

            if (opcode == 71 && length >= 3) {
                uint32_t target_id = words[i + 1];
                uint32_t decoration = words[i + 2];

                if (decoration == 33 && length >= 4) {
                    auto& dec = decorations[target_id];
                    dec.binding = words[i + 3];
                    dec.has_binding = true;
                } else if (decoration == 34 && length >= 4) {
                    auto& dec = decorations[target_id];
                    dec.set = words[i + 3];
                    dec.has_set = true;
                }
            }

            i += length;
        }

        for (const auto& [id, dec] : decorations) {
            if (dec.has_set && dec.has_binding) {
                active_slots.insert(MakeSlotKey(dec.set, dec.binding));
            }
        }

        return active_slots;
    }

    tryengine::graphics::ShaderReflectionData FilterReflectionByActiveSlots(
        tryengine::graphics::ShaderReflectionData reflection,
        const eastl::hash_set<uint64_t>& active_slots) const
    {
        tryengine::graphics::ShaderReflectionData filtered{};
        for (auto& binding : reflection.bindings) {
            uint64_t key = MakeSlotKey(binding.set, binding.binding);
            if (active_slots.find(key) != active_slots.end()) {
                filtered.bindings.push_back(std::move(binding));
            }
        }
        return filtered;
    }

    void CountStageResourcesFromReflection(
        const tryengine::graphics::ShaderReflectionData& reflection,
        tryengine::graphics::StageResourceCounts& counts) const
    {
        using namespace tryengine::graphics;
        for (const auto& binding : reflection.bindings) {
            if (binding.kind == ShaderResourceKind::UniformBuffer) {
                counts.num_uniform_buffers++;
            } else if (binding.kind == ShaderResourceKind::StorageBufferRead || binding.kind == ShaderResourceKind::StorageBufferWrite) {
                counts.num_storage_buffers++;
            } else if (binding.kind == ShaderResourceKind::SampledTexture) {
                counts.num_samplers++;
            } else if (binding.kind == ShaderResourceKind::StorageTexture) {
                counts.num_storage_textures++;
            }
        }
    }

    bool IsStorageBufferType(slang::TypeReflection* type) const {
        if (!type) return false;

        slang::TypeReflection::Kind kind = type->getKind();
        if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer) {
            return true;
        }

        if (kind == slang::TypeReflection::Kind::Resource) {
            SlangResourceShape base_shape = static_cast<SlangResourceShape>(type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK);
            return base_shape == SLANG_STRUCTURED_BUFFER || base_shape == SLANG_BYTE_ADDRESS_BUFFER;
        }

        return false;
    }

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

    tryengine::graphics::ShaderReflectionData ExtractReflection(
        slang::ProgramLayout* layout,
        tryengine::graphics::ShaderStage stage = tryengine::graphics::ShaderStage::Fragment) const
    {
        tryengine::graphics::ShaderReflectionData out_reflection{};
        if (!layout) return out_reflection;

        uint32_t param_count = layout->getParameterCount();
        for (uint32_t i = 0; i < param_count; ++i) {
            slang::VariableLayoutReflection* var = layout->getParameterByIndex(i);
            slang::TypeLayoutReflection* type_layout = var->getTypeLayout();
            slang::TypeReflection* type = type_layout->getType();

            tryengine::graphics::ShaderReflectedBinding binding{};
            binding.name = var->getName() ? var->getName() : "";
            binding.name_hash = tryengine::graphics::RGTagOf(binding.name);
            binding.binding = var->getBindingIndex();
            binding.set = static_cast<uint32_t>(var->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));
            binding.size = static_cast<uint32_t>(type_layout->getSize());
            binding.stage = stage;

            binding.scope = DetermineBindingScope(var);

            slang::TypeReflection::Kind kind = type->getKind();
            SlangResourceAccess access = type->getResourceAccess();
            bool is_writeable = (access == SLANG_RESOURCE_ACCESS_READ_WRITE || access == SLANG_RESOURCE_ACCESS_WRITE);

            if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock) {
                binding.kind = tryengine::graphics::ShaderResourceKind::UniformBuffer;

                slang::TypeLayoutReflection* element_layout = type_layout->getElementTypeLayout();
                if (element_layout) {
                    // ВАЖНО: type_layout->getSize() без категории считает размер в "родной" категории
                    // самого параметра (для ConstantBuffer/ParameterBlock это дескрипторный слот, а не байты),
                    // из-за чего он всегда был 0. Реальный байтовый размер UBO лежит на element_layout
                    // (внутренний struct) в категории Uniform — так же, как ниже считаются offset/size полей.
                    binding.size = static_cast<uint32_t>(element_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));

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
            } else if (IsStorageBufferType(type)) {
                binding.kind = is_writeable
                    ? tryengine::graphics::ShaderResourceKind::StorageBufferWrite
                    : tryengine::graphics::ShaderResourceKind::StorageBufferRead;
            } else if (kind == slang::TypeReflection::Kind::Resource) {
                binding.kind = is_writeable
                    ? tryengine::graphics::ShaderResourceKind::StorageTexture
                    : tryengine::graphics::ShaderResourceKind::SampledTexture;
            } else if (kind == slang::TypeReflection::Kind::SamplerState) {
                binding.kind = tryengine::graphics::ShaderResourceKind::SampledTexture;
            }

            out_reflection.bindings.push_back(std::move(binding));
        }

        return out_reflection;
    }

    void DumpStageSection(
        eastl::string& out,
        const char* stage_title,
        const tryengine::graphics::ShaderReflectionData& reflection) const
    {
        using namespace tryengine::fmt;

        format_append(out, "========================================================\n");
        format_append(out, "  {}\n", stage_title);
        format_append(out, "========================================================\n");

        if (reflection.bindings.empty()) {
            out.append("  <No resources bound for this stage>\n\n");
            return;
        }

        for (const auto& binding : reflection.bindings) {
            format_append(out, "Resource: \"{}\" (Hash: 0x{:X})\n", binding.name.c_str(), binding.name_hash);
            format_append(out, "  - Scope: {} | Kind: {}\n", ScopeToString(binding.scope), KindToString(binding.kind));
            format_append(out, "  - Set: {}, Binding: {}, Slot: {}, Size: {} bytes\n", binding.set, binding.binding, binding.slot, binding.size);

            if (!binding.params.empty()) {
                out.append("  - Members:\n");
                for (const auto& param : binding.params) {
                    format_append(out, "      * \"{}\" | Offset: {} | Size: {} | Type: {}\n",
                                  param.name.c_str(),
                                  param.offset,
                                  param.size,
                                  ParamTypeToString(param.type));
                }
            }
            out.append("--------------------------------------------------------\n");
        }
        out.append("\n");
    }

    eastl::string DumpReflectionToString(
        const tryengine::graphics::ShaderReflectionData& vs_reflection,
        const tryengine::graphics::ShaderReflectionData& fs_reflection,
        const tryengine::graphics::ShaderReflectionData& full_reflection) const
    {
        eastl::string out;
        out.append("================ SHADER REFLECTION DUMP ================\n\n");

        DumpStageSection(out, "VERTEX SHADER BINDINGS", vs_reflection);
        DumpStageSection(out, "FRAGMENT SHADER BINDINGS", fs_reflection);
        DumpStageSection(out, "FULL PROGRAM COMBINED BINDINGS", full_reflection);

        return out;
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