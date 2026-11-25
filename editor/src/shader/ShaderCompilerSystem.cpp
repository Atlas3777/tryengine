#include "editor/shader/ShaderCompilerSystem.hpp"

#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/sort.h>
#include <EASTL/string_view.h>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <utility>

#include "engine/core/Assert.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/Log.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/binary/MeshBinary.hpp"

namespace tryeditor {

namespace {

using Slang::ComPtr;
using tryengine::core::LogAndMakeError;
using tryengine::graphics::ShaderParamType;
using tryengine::graphics::ShaderReflectedBinding;
using tryengine::graphics::ShaderReflectedParam;
using tryengine::graphics::ShaderReflectionData;
using tryengine::graphics::ShaderResourceKind;
using tryengine::graphics::StageResourceCounts;

eastl::string FromStd(std::string_view value) {
    return eastl::string(value.data(), value.size());
}

std::filesystem::path AbsolutePath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec)
        absolute = path;
    return absolute.lexically_normal();
}

// Путь относительно cwd, если файл лежит внутри проекта. Штампы и логи читаемее, а
// сравнение в IsUnder работает с абсолютными путями одинаково.
eastl::string ToProjectPath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        const std::filesystem::path relative = path.lexically_relative(cwd);
        if (!relative.empty() && relative.native().rfind("..", 0) != 0)
            return FromStd(relative.generic_string());
    }
    return FromStd(path.generic_string());
}

bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return !relative.empty() && relative.native().rfind("..", 0) != 0;
}

bool IsIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c) {
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

// Маска атрибутов меша, которую объявляет сам шейдер:
//
//   public static const uint MESH_ATTRIBUTES = ATTR_BIT_NORMAL | ATTR_BIT_TEXCOORD0;
//
// Slang не отдаёт её через рефлексию (атрибуты читаются из BDA, а не как vertex input),
// поэтому объявление разбирается из исходника. Это контракт шейдера с материалом: редактор
// предлагает материал только мешам, у которых есть все перечисленные атрибуты. Токены
// сверяются с MeshAttributeFlags, поэтому опечатка в имени — ошибка импорта, а не тихая
// потеря ограничения.
tryengine::Result<uint32_t> ParseRequiredMeshAttributes(eastl::string_view source) {
    constexpr eastl::string_view marker = "MESH_ATTRIBUTES";

    // Find the declaration, not a mention in a comment: the marker must be a whole identifier
    // followed by '='. `MESH_ATTRIBUTES_NOT_MESH` is skipped by the identifier check.
    size_t marker_pos = eastl::string_view::npos;
    size_t equals_pos = eastl::string_view::npos;
    for (size_t at = source.find(marker); at != eastl::string_view::npos; at = source.find(marker, at + 1)) {
        if (at > 0 && IsIdentChar(source[at - 1]))
            continue;
        size_t next = at + marker.size();
        if (next < source.size() && IsIdentChar(source[next]))
            continue;
        while (next < source.size() &&
               (source[next] == ' ' || source[next] == '\t' || source[next] == '\r' || source[next] == '\n')) {
            ++next;
        }
        if (next < source.size() && source[next] == '=') {
            marker_pos = at;
            equals_pos = next;
            break;
        }
    }

    if (marker_pos == eastl::string_view::npos) {
        return LogAndMakeError(
            "Slang: shader must declare `public static const uint MESH_ATTRIBUTES = ...;` so the "
            "editor knows which mesh attributes it reads (use 0 for position-only shaders)");
    }

    const size_t semicolon_pos = source.find(';', equals_pos + 1);
    if (semicolon_pos == eastl::string_view::npos)
        return LogAndMakeError("Slang: MESH_ATTRIBUTES declaration has no terminating ';'");

    const eastl::string_view expression = source.substr(equals_pos + 1, semicolon_pos - equals_pos - 1);
    uint32_t mask = 0;
    size_t cursor = 0;
    while (cursor < expression.size()) {
        const char c = expression[cursor];
        if (c == '|' || c == '(' || c == ')' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++cursor;
            continue;
        }

        if (IsIdentStart(c)) {
            const size_t begin = cursor;
            while (cursor < expression.size() && IsIdentChar(expression[cursor]))
                ++cursor;
            const eastl::string_view token = expression.substr(begin, cursor - begin);
            tryengine::resources::MeshAttributeFlags flag{};
            if (!tryengine::resources::ParseMeshAttributeToken(token, flag)) {
                return LogAndMakeError("Slang: unknown mesh attribute token '{}' in MESH_ATTRIBUTES",
                                       eastl::string(token.data(), token.size()).c_str());
            }
            mask |= static_cast<uint32_t>(flag);
            continue;
        }

        if (c >= '0' && c <= '9') {
            const size_t begin = cursor;
            while (cursor < expression.size()) {
                const char digit = expression[cursor];
                const bool hex = (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
                const bool numeric = (digit >= '0' && digit <= '9') || hex || digit == 'x' || digit == 'X' ||
                                     digit == 'u' || digit == 'U';
                if (!numeric)
                    break;
                ++cursor;
            }
            const eastl::string_view token = expression.substr(begin, cursor - begin);
            if (token == "0" || token == "0u" || token == "0U")
                continue;
            if (token == "0xFFFFFFFE" || token == "0xFFFFFFFEu" || token == "0xFFFFFFFEU") {
                mask |= tryengine::resources::kNotMeshMaterialAttributes;
                continue;
            }
            return LogAndMakeError(
                "Slang: MESH_ATTRIBUTES must be built from ATTR_BIT_* tokens, 0, or the not-a-mesh "
                "sentinel 0xFFFFFFFEu");
        }

        const char unexpected[2] = {c, '\0'};
        return LogAndMakeError("Slang: unexpected character '{}' in MESH_ATTRIBUTES", unexpected);
    }

    return mask;
}

bool IsUnderAny(const std::filesystem::path& path, const eastl::vector<std::filesystem::path>& roots) {
    for (const auto& root : roots)
        if (IsUnder(path, root))
            return true;
    return false;
}

void FindEntryPoint(slang::IModule* module, std::initializer_list<const char*> names, ComPtr<slang::IEntryPoint>& out) {
    for (const char* name : names) {
        module->findEntryPointByName(name, out.writeRef());
        if (out)
            return;
    }
}

// ---------------------------------------------------------------------------
// Рефлексия. Всё, что ниже, — перенос логики SlangImporter: компилятор отвечает за
// извлечение данных из layout, импортёр — только за артефакты.
// ---------------------------------------------------------------------------

const char* KindToString(ShaderResourceKind kind) {
    switch (kind) {
        default:
            TRY_ASSERT(false, "Unhandled ShaderResourceKind");
            return "Unhandled ShaderResourceKind";
        case ShaderResourceKind::UniformBuffer:
            return "UniformBuffer";
        case ShaderResourceKind::SampledTexture:
            return "SampledTexture";
        case ShaderResourceKind::StorageBufferRead:
            return "StorageBufferRead";
        case ShaderResourceKind::StorageBufferWrite:
            return "StorageBufferWrite";
        case ShaderResourceKind::StorageTexture:
            return "StorageTexture";
    }
}

const char* SetToString(uint32_t set) {
    switch (static_cast<tryengine::graphics::BindingScope>(set)) {
        default:
            TRY_ASSERT(false, "Unhandled BindingScope");
            return "Unhandled BindingScope";
        case tryengine::graphics::BindingScope::Pass:
            return "Pass (Set 0)";
        case tryengine::graphics::BindingScope::Material:
            return "Material (Set 1)";
        case tryengine::graphics::BindingScope::PerObj:
            return "PerObj (Set 2)";
    }
}

const char* ParamTypeToString(ShaderParamType type) {
    switch (type) {
        default:
            TRY_ASSERT(false, "Unhandled ShaderParamType");
            return "Unhandled ShaderParamType";
        case ShaderParamType::Float:
            return "Float";
        case ShaderParamType::Int:
            return "Int";
        case ShaderParamType::Vec2:
            return "Vec2";
        case ShaderParamType::Vec3:
            return "Vec3";
        case ShaderParamType::Vec4:
            return "Vec4";
        case ShaderParamType::Mat3:
            return "Mat3";
        case ShaderParamType::Mat4:
            return "Mat4";
    }
}

uint64_t MakeSlotKey(uint32_t set, uint32_t binding) {
    return (static_cast<uint64_t>(set) << 32) | static_cast<uint64_t>(binding);
}

eastl::hash_set<uint64_t> ExtractActiveSpirvSlots(const eastl::vector<uint8_t>& spirv) {
    eastl::hash_set<uint64_t> active_slots;
    if (spirv.size() < 20)
        return active_slots;

    const uint32_t* words = reinterpret_cast<const uint32_t*>(spirv.data());
    const size_t word_count = spirv.size() / sizeof(uint32_t);

    if (words[0] != 0x07230203)
        return active_slots;

    struct IdDecorations {
        uint32_t set = 0;
        uint32_t binding = 0;
        bool has_set = false;
        bool has_binding = false;
    };

    eastl::hash_map<uint32_t, IdDecorations> decorations;

    size_t i = 5;
    while (i < word_count) {
        const uint32_t instruction = words[i];
        const uint16_t opcode = instruction & 0xFFFF;
        const uint16_t length = instruction >> 16;

        if (length == 0 || i + length > word_count)
            break;

        if (opcode == 71 && length >= 3) {
            const uint32_t target_id = words[i + 1];
            const uint32_t decoration = words[i + 2];

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

    for (const auto& [id, dec] : decorations)
        if (dec.has_set && dec.has_binding)
            active_slots.insert(MakeSlotKey(dec.set, dec.binding));

    return active_slots;
}

ShaderReflectionData FilterReflectionByActiveSlots(ShaderReflectionData reflection,
                                                   const eastl::hash_set<uint64_t>& active_slots) {
    ShaderReflectionData filtered{};
    for (auto& binding : reflection.bindings) {
        const uint64_t key = MakeSlotKey(binding.set, binding.binding);
        if (active_slots.find(key) != active_slots.end())
            filtered.bindings.push_back(std::move(binding));
    }
    return filtered;
}

void CountStageResourcesFromReflection(const ShaderReflectionData& reflection, StageResourceCounts& counts) {
    for (const auto& binding : reflection.bindings) {
        if (binding.kind == ShaderResourceKind::UniformBuffer) {
            counts.num_uniform_buffers++;
        } else if (binding.kind == ShaderResourceKind::StorageBufferRead ||
                   binding.kind == ShaderResourceKind::StorageBufferWrite) {
            counts.num_storage_buffers++;
        } else if (binding.kind == ShaderResourceKind::SampledTexture) {
            counts.num_samplers++;
        } else if (binding.kind == ShaderResourceKind::StorageTexture) {
            counts.num_storage_textures++;
        }
    }
}

bool IsStorageBufferType(slang::TypeReflection* type) {
    if (!type)
        return false;

    const slang::TypeReflection::Kind kind = type->getKind();
    if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer)
        return true;

    if (kind == slang::TypeReflection::Kind::Resource) {
        const SlangResourceShape base_shape =
            static_cast<SlangResourceShape>(type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK);
        return base_shape == SLANG_STRUCTURED_BUFFER || base_shape == SLANG_BYTE_ADDRESS_BUFFER;
    }

    return false;
}

ShaderParamType MapSlangType(slang::TypeReflection* type) {
    const slang::TypeReflection::Kind kind = type->getKind();

    if (kind == slang::TypeReflection::Kind::Scalar) {
        const slang::TypeReflection::ScalarType scalar = type->getScalarType();

        if (scalar == slang::TypeReflection::ScalarType::Int32 || scalar == slang::TypeReflection::ScalarType::UInt32 ||
            scalar == slang::TypeReflection::ScalarType::Bool) {
            return ShaderParamType::Int;
        }
        return ShaderParamType::Float;
    }
    if (kind == slang::TypeReflection::Kind::Vector) {
        const uint32_t elem_count = type->getElementCount();
        if (elem_count == 2)
            return ShaderParamType::Vec2;
        if (elem_count == 3)
            return ShaderParamType::Vec3;
        if (elem_count == 4)
            return ShaderParamType::Vec4;
    }
    if (kind == slang::TypeReflection::Kind::Matrix) {
        const uint32_t rows = type->getRowCount();
        if (rows == 3)
            return ShaderParamType::Mat3;
        if (rows == 4)
            return ShaderParamType::Mat4;
    }
    return ShaderParamType::Float;
}

ShaderReflectionData ExtractReflection(slang::ProgramLayout* layout) {
    ShaderReflectionData out_reflection{};
    if (!layout)
        return out_reflection;

    const uint32_t param_count = layout->getParameterCount();
    for (uint32_t i = 0; i < param_count; ++i) {
        slang::VariableLayoutReflection* var = layout->getParameterByIndex(i);
        slang::TypeLayoutReflection* type_layout = var->getTypeLayout();
        slang::TypeReflection* type = type_layout->getType();

        ShaderReflectedBinding binding{};
        binding.name = var->getName() ? var->getName() : "";
        binding.name_hash = tryengine::graphics::RGTagOf(binding.name);
        binding.binding = var->getBindingIndex();
        binding.set = static_cast<uint32_t>(var->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));
        binding.size = static_cast<uint32_t>(type_layout->getSize());

        const slang::TypeReflection::Kind kind = type->getKind();
        const SlangResourceAccess access = type->getResourceAccess();
        const bool is_writeable = (access == SLANG_RESOURCE_ACCESS_READ_WRITE || access == SLANG_RESOURCE_ACCESS_WRITE);

        if (kind == slang::TypeReflection::Kind::ConstantBuffer ||
            kind == slang::TypeReflection::Kind::ParameterBlock) {
            binding.kind = ShaderResourceKind::UniformBuffer;

            slang::TypeLayoutReflection* element_layout = type_layout->getElementTypeLayout();
            if (element_layout) {
                binding.size = static_cast<uint32_t>(element_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));

                const uint32_t field_count = element_layout->getFieldCount();
                for (uint32_t f = 0; f < field_count; ++f) {
                    slang::VariableLayoutReflection* field_var = element_layout->getFieldByIndex(f);
                    slang::TypeLayoutReflection* field_type_layout = field_var->getTypeLayout();

                    ShaderReflectedParam param{};
                    param.name = field_var->getName() ? field_var->getName() : "";
                    param.name_hash = tryengine::graphics::RGTagOf(param.name);
                    param.offset = static_cast<uint32_t>(field_var->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
                    param.size = static_cast<uint32_t>(field_type_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
                    param.type = MapSlangType(field_type_layout->getType());

                    binding.params.push_back(std::move(param));
                }
            }
        } else if (IsStorageBufferType(type)) {
            binding.kind =
                is_writeable ? ShaderResourceKind::StorageBufferWrite : ShaderResourceKind::StorageBufferRead;
        } else if (kind == slang::TypeReflection::Kind::Resource) {
            binding.kind = is_writeable ? ShaderResourceKind::StorageTexture : ShaderResourceKind::SampledTexture;
        } else if (kind == slang::TypeReflection::Kind::SamplerState) {
            binding.kind = ShaderResourceKind::SampledTexture;
        }

        out_reflection.bindings.push_back(std::move(binding));
    }

    return out_reflection;
}

void DumpStageSection(eastl::string& out, const char* stage_title, const ShaderReflectionData& reflection) {
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
        format_append(out, "  - Scope: {} | Kind: {}\n", SetToString(binding.set), KindToString(binding.kind));
        format_append(out, "  - Set: {}, Binding: {}, Size: {} bytes\n", binding.set, binding.binding, binding.size);

        if (!binding.params.empty()) {
            out.append("  - Members:\n");
            for (const auto& param : binding.params) {
                format_append(out, "      * \"{}\" | Offset: {} | Size: {} | Type: {}\n", param.name.c_str(),
                              param.offset, param.size, ParamTypeToString(param.type));
            }
        }
        out.append("--------------------------------------------------------\n");
    }
    out.append("\n");
}

eastl::string DumpReflectionToString(const ShaderReflectionData& vs_reflection,
                                     const ShaderReflectionData& fs_reflection,
                                     const ShaderReflectionData& full_reflection, uint32_t required_attributes) {
    eastl::string out;
    out.append("================ SHADER REFLECTION DUMP ================\n\n");
    out.append("Required mesh attributes: ");
    out.append(tryengine::resources::DescribeMeshAttributeFlags(required_attributes).c_str());
    out.append("\n\n");

    DumpStageSection(out, "VERTEX SHADER BINDINGS", vs_reflection);
    DumpStageSection(out, "FRAGMENT SHADER BINDINGS", fs_reflection);
    DumpStageSection(out, "FULL PROGRAM COMBINED BINDINGS", full_reflection);

    return out;
}

void CollectDependencies(slang::ISession* session, const std::filesystem::path& source_path,
                         const eastl::vector<std::filesystem::path>& search_roots,
                         eastl::vector<ShaderDependency>& out) {
    eastl::hash_set<eastl::string> seen;
    auto add = [&](const char* name, const char* file) {
        if (!file || !*file)
            return;

        const std::filesystem::path absolute = AbsolutePath(std::filesystem::path(file));
        if (absolute == source_path || !IsUnderAny(absolute, search_roots))
            return;

        eastl::string key = FromStd(absolute.generic_string());
        if (!seen.insert(key).second)
            return;

        ShaderDependency dependency{};
        dependency.name = name && *name ? FromStd(name) : FromStd(absolute.stem().string());
        dependency.path = ToProjectPath(absolute);
        out.push_back(std::move(dependency));
    };

    const SlangInt module_count = session->getLoadedModuleCount();
    for (SlangInt i = 0; i < module_count; ++i) {
        slang::IModule* module = session->getLoadedModule(i);
        if (!module)
            continue;

        // Сам исходник в зависимости не попадает: его изменения уже отслеживает штамп
        // (проверка absolute == source_path внутри add).
        add(module->getName(), module->getFilePath());

        const SlangInt dependency_count = module->getDependencyFileCount();
        for (SlangInt d = 0; d < dependency_count; ++d) {
            const char* file = module->getDependencyFilePath(d);
            if (file && *file)
                add(nullptr, file);
        }
    }

    eastl::sort(out.begin(), out.end(),
                [](const ShaderDependency& lhs, const ShaderDependency& rhs) { return lhs.path < rhs.path; });
}

}  // namespace

struct ShaderCompilerSystem::CompilerContext {
    ComPtr<slang::IGlobalSession> global;
    SlangProfileID profile = SLANG_PROFILE_UNKNOWN;
    std::mutex mutex;
};

ShaderCompilerSystem::ShaderCompilerSystem(ShaderCompilerConfig config) : config_(eastl::move(config)) {
    if (config_.context_count == 0)
        config_.context_count = 1;

    contexts_.reserve(config_.context_count);
    for (uint32_t i = 0; i < config_.context_count; ++i)
        contexts_.push_back(eastl::make_unique<CompilerContext>());
}

ShaderCompilerSystem::~ShaderCompilerSystem() = default;

tryengine::Result<ShaderCompileResult> ShaderCompilerSystem::Compile(const ShaderCompileRequest& request) {
    if (request.source.empty())
        return LogAndMakeError("Slang: source is empty");
    if (contexts_.empty())
        return LogAndMakeError("Slang: compiler has no contexts");

    const uint32_t context_index =
        next_context_.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32_t>(contexts_.size());
    CompilerContext& context = *contexts_[context_index];
    std::lock_guard lock(context.mutex);

    if (!context.global) {
        if (SLANG_FAILED(slang::createGlobalSession(context.global.writeRef())))
            return LogAndMakeError("Slang: Failed to create global session");
        context.profile = context.global->findProfile("spirv_1_6");
        if (context.profile == SLANG_PROFILE_UNKNOWN)
            return LogAndMakeError("Slang: Unknown profile 'spirv_1_6'");
    }

    if (request.path.empty())
        return LogAndMakeError("Slang: source path is empty");

    const std::filesystem::path source_path =
        AbsolutePath(std::filesystem::path(std::string(request.path.data(), request.path.size())));

    // Каталог исходника идёт первым: `import Sibling;` находит соседний модуль без
    // дополнительной настройки. Остальные корни — из конфигурации системы.
    eastl::vector<eastl::string> search_paths;
    eastl::vector<std::filesystem::path> search_roots;
    auto add_search_path = [&](const std::filesystem::path& path) {
        const std::filesystem::path absolute = AbsolutePath(path);
        const eastl::string text = FromStd(absolute.generic_string());
        for (const auto& existing : search_paths)
            if (existing == text)
                return;
        search_paths.push_back(text);
        search_roots.push_back(absolute);
    };
    add_search_path(source_path.parent_path());
    for (const auto& module_search_path : config_.module_search_paths)
        add_search_path(std::string(module_search_path.data(), module_search_path.size()));

    eastl::vector<const char*> search_path_ptrs;
    search_path_ptrs.reserve(search_paths.size());
    for (const auto& path : search_paths)
        search_path_ptrs.push_back(path.c_str());

    slang::TargetDesc target_desc{};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = context.profile;

    slang::SessionDesc session_desc{};
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;
    session_desc.searchPaths = search_path_ptrs.data();
    session_desc.searchPathCount = static_cast<SlangInt>(search_path_ptrs.size());

    ComPtr<slang::ISession> session;
    if (SLANG_FAILED(context.global->createSession(session_desc, session.writeRef())))
        return LogAndMakeError("Slang: Failed to create session");

    const eastl::string source_path_text = FromStd(source_path.generic_string());
    const eastl::string module_name = FromStd(source_path.stem().string());

    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(module_name.c_str(), source_path_text.c_str(),
                                                                 request.source.c_str(), diagnostics.writeRef());

    if (diagnostics && diagnostics->getBufferSize() > 0) {
        LogError(LogCategory::Importer, "Slang diagnostics (module compiled = {}):\n{}", module != nullptr,
                 static_cast<const char*>(diagnostics->getBufferPointer()));
    }

    if (!module) {
        const char* error_message =
            diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Unknown error";
        return LogAndMakeError("Slang compilation error:\n{}", error_message);
    }

    ComPtr<slang::IEntryPoint> vs_entry;
    ComPtr<slang::IEntryPoint> fs_entry;
    FindEntryPoint(module, {"vsMain", "mainVS", "main"}, vs_entry);
    FindEntryPoint(module, {"fsMain", "mainFS", "main"}, fs_entry);

    ShaderCompileResult result{};

    if (vs_entry && fs_entry) {
        auto required_attributes =
            ParseRequiredMeshAttributes(eastl::string_view(request.source.c_str(), request.source.size()));
        if (!required_attributes.has_value())
            return std::unexpected(eastl::move(required_attributes.error()));
        result.shader.reflection.required_attributes = *required_attributes;

        slang::IComponentType* vs_components[] = {module, vs_entry.get()};
        ComPtr<slang::IComponentType> vs_program;
        if (SLANG_FAILED(session->createCompositeComponentType(vs_components, 2, vs_program.writeRef(),
                                                               diagnostics.writeRef()))) {
            return LogAndMakeError("Slang: Failed to create VS composite component");
        }

        slang::IComponentType* fs_components[] = {module, fs_entry.get()};
        ComPtr<slang::IComponentType> fs_program;
        if (SLANG_FAILED(session->createCompositeComponentType(fs_components, 2, fs_program.writeRef(),
                                                               diagnostics.writeRef()))) {
            return LogAndMakeError("Slang: Failed to create FS composite component");
        }

        slang::IComponentType* full_components[] = {module, vs_entry.get(), fs_entry.get()};
        ComPtr<slang::IComponentType> full_program;
        if (SLANG_FAILED(session->createCompositeComponentType(full_components, 3, full_program.writeRef(),
                                                               diagnostics.writeRef()))) {
            return LogAndMakeError("Slang: Failed to create full composite component");
        }

        ComPtr<slang::IComponentType> vs_linked;
        ComPtr<slang::IComponentType> fs_linked;
        ComPtr<slang::IComponentType> full_linked;

        diagnostics = nullptr;
        if (SLANG_FAILED(vs_program->link(vs_linked.writeRef(), diagnostics.writeRef())) || !vs_linked) {
            const char* error_message =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Link error";
            return LogAndMakeError("Slang: VS Link failed:\n{}", error_message);
        }

        diagnostics = nullptr;
        if (SLANG_FAILED(fs_program->link(fs_linked.writeRef(), diagnostics.writeRef())) || !fs_linked) {
            const char* error_message =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Link error";
            return LogAndMakeError("Slang: FS Link failed:\n{}", error_message);
        }

        diagnostics = nullptr;
        if (SLANG_FAILED(full_program->link(full_linked.writeRef(), diagnostics.writeRef())) || !full_linked) {
            const char* error_message =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Link error";
            return LogAndMakeError("Slang: Full program Link failed:\n{}", error_message);
        }

        ComPtr<slang::IBlob> vs_spirv;
        ComPtr<slang::IBlob> fs_spirv;

        diagnostics = nullptr;
        if (SLANG_FAILED(vs_linked->getEntryPointCode(0, 0, vs_spirv.writeRef(), diagnostics.writeRef())) ||
            !vs_spirv) {
            const char* error_message =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "CodeGen error";
            return LogAndMakeError("Slang: Failed to generate VS SPIR-V code:\n{}", error_message);
        }

        diagnostics = nullptr;
        if (SLANG_FAILED(fs_linked->getEntryPointCode(0, 0, fs_spirv.writeRef(), diagnostics.writeRef())) ||
            !fs_spirv) {
            const char* error_message =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "CodeGen error";
            return LogAndMakeError("Slang: Failed to generate FS SPIR-V code:\n{}", error_message);
        }

        result.shader.vertex_spv.assign(
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(vs_spirv->getBufferPointer()) + vs_spirv->getBufferSize());
        result.shader.fragment_spv.assign(
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()),
            static_cast<const uint8_t*>(fs_spirv->getBufferPointer()) + fs_spirv->getBufferSize());

        const eastl::hash_set<uint64_t> vs_active_slots = ExtractActiveSpirvSlots(result.shader.vertex_spv);
        const eastl::hash_set<uint64_t> fs_active_slots = ExtractActiveSpirvSlots(result.shader.fragment_spv);

        const ShaderReflectionData vs_reflection =
            FilterReflectionByActiveSlots(ExtractReflection(vs_linked->getLayout()), vs_active_slots);
        const ShaderReflectionData fs_reflection =
            FilterReflectionByActiveSlots(ExtractReflection(fs_linked->getLayout()), fs_active_slots);

        result.shader.reflection.bindings.insert(result.shader.reflection.bindings.end(),
                                                 vs_reflection.bindings.begin(), vs_reflection.bindings.end());
        result.shader.reflection.bindings.insert(result.shader.reflection.bindings.end(),
                                                 fs_reflection.bindings.begin(), fs_reflection.bindings.end());

        const ShaderReflectionData full_reflection = ExtractReflection(full_linked->getLayout());

        CountStageResourcesFromReflection(vs_reflection, result.shader.vertex_counts);
        CountStageResourcesFromReflection(fs_reflection, result.shader.fragment_counts);

        result.reflection_dump = DumpReflectionToString(vs_reflection, fs_reflection, full_reflection,
                                                        result.shader.reflection.required_attributes);
    } else if (!vs_entry && !fs_entry) {
        return LogAndMakeError(
            "Slang: '{}' has no shader entry points. Shared code belongs in shader_modules/ and "
            "is imported with `import <module>;`; only shaders with vsMain/fsMain are assets.",
            module_name.c_str());
    } else {
        return LogAndMakeError("Slang: expected both vertex and fragment entry points, found {}",
                               vs_entry ? "only vertex" : "only fragment");
    }

    CollectDependencies(session.get(), source_path, search_roots, result.dependencies);

    return result;
}

}  // namespace tryeditor
