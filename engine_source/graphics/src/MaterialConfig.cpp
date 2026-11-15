#include "engine/graphics/MaterialConfig.hpp"

#include <charconv>
#include <string_view>
#include <utility>

#include "engine/core/MakeError.hpp"

namespace tryengine::graphics {

namespace {

using tryengine::core::Error;
using tryengine::resources::ConfigArray;
using tryengine::resources::ConfigField;
using tryengine::resources::ConfigObject;
using tryengine::resources::ConfigValue;

ConfigField MakeStringField(eastl::string name, eastl::string type_name, eastl::string value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = eastl::move(type_name);
    field.value = eastl::move(value);
    return field;
}

ConfigField MakeU32Field(eastl::string name, eastl::string type_name, uint32_t value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = eastl::move(type_name);
    field.value = value;
    return field;
}

ConfigField MakeObjectElement(eastl::string type_name, ConfigObject object) {
    ConfigField field;
    field.type_name = eastl::move(type_name);
    field.value = eastl::make_unique<ConfigObject>(eastl::move(object));
    return field;
}

ConfigField MakeBlockField(const char* name, const char* type_name, const ConfigObject& object) {
    ConfigField field;
    field.name = name;
    field.type_name = type_name;
    field.value = eastl::make_unique<ConfigObject>(object);
    return field;
}

bool ReadString(const ConfigValue& value, eastl::string& out) {
    const auto* string_value = eastl::get_if<eastl::string>(&value);
    if (!string_value) {
        return false;
    }
    out = *string_value;
    return true;
}

bool ReadU64(const ConfigValue& value, uint64_t& out) {
    if (const auto* uint_value = eastl::get_if<uint64_t>(&value)) {
        out = *uint_value;
        return true;
    }
    if (const auto* int_value = eastl::get_if<int64_t>(&value); int_value && *int_value >= 0) {
        out = static_cast<uint64_t>(*int_value);
        return true;
    }
    if (const auto* uint32_value = eastl::get_if<uint32_t>(&value)) {
        out = *uint32_value;
        return true;
    }
    if (const auto* int32_value = eastl::get_if<int32_t>(&value); int32_value && *int32_value >= 0) {
        out = static_cast<uint64_t>(*int32_value);
        return true;
    }
    if (const auto* string_value = eastl::get_if<eastl::string>(&value)) {
        eastl::string_view token(string_value->data(), string_value->size());
        if (!token.empty() && token.front() == '+') {
            token.remove_prefix(1);
        }
        if (token.empty()) {
            return false;
        }
        const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
        return ec == std::errc{} && ptr == token.data() + token.size();
    }
    return false;
}

}  // namespace

Result<MaterialConfig> ParseMaterialConfig(eastl::string_view text) {
    auto parsed = tryengine::resources::ParseConfig(text);
    if (!parsed.has_value()) {
        return std::unexpected(eastl::move(parsed.error()));
    }

    ConfigObject root = eastl::move(*parsed);
    MaterialConfig config;

    for (ConfigField& field : root.fields) {
        if (field.name == "name") {
            if (!ReadString(field.value, config.name)) {
                return LogAndMakeError("Material config: field 'name' must be a string");
            }
            continue;
        }
        if (field.name == "shader") {
            if (!ReadU64(field.value, config.shader)) {
                return LogAndMakeError("Material config: field 'shader' must be a u64");
            }
            continue;
        }
        if (field.name == "params" || field.name == "textures") {
            ConfigObject* block = tryengine::resources::GetObject(field.value);
            if (!block) {
                return LogAndMakeError("Material config: field '{}' must be an object", field.name.c_str());
            }
            if (field.name == "params") {
                config.params = eastl::move(*block);
            } else {
                config.textures = eastl::move(*block);
            }
            continue;
        }

        return LogAndMakeError("Material config: unknown field '{}'", field.name.c_str());
    }

    if (config.shader == 0) {
        return LogAndMakeError("Material config: field 'shader' is missing or zero");
    }
    return config;
}

Result<MaterialConfig> ParseMaterialConfig(eastl::span<const uint8_t> bytes) {
    return ParseMaterialConfig(
        eastl::string_view(reinterpret_cast<const char*>(bytes.data()), static_cast<size_t>(bytes.size())));
}

eastl::string WriteMaterialConfig(const MaterialConfig& config) {
    ConfigObject root;

    ConfigField name;
    name.name = "name";
    name.type_name = "string";
    name.value = config.name;
    root.fields.push_back(eastl::move(name));

    ConfigField shader;
    shader.name = "shader";
    shader.type_name = "u64";
    shader.value = config.shader;
    root.fields.push_back(eastl::move(shader));

    if (!config.params.fields.empty()) {
        root.fields.push_back(MakeBlockField("params", "Params", config.params));
    }
    if (!config.textures.fields.empty()) {
        root.fields.push_back(MakeBlockField("textures", "Textures", config.textures));
    }

    return tryengine::resources::WriteConfig(root);
}

const char* ShaderParamTypeToConfigType(ShaderParamType type) {
    switch (type) {
        case ShaderParamType::Float:
            return "f32";
        case ShaderParamType::Int:
            return "i32";
        case ShaderParamType::Vec2:
            return "f32x2";
        case ShaderParamType::Vec3:
            return "f32x3";
        case ShaderParamType::Vec4:
            return "f32x4";
        case ShaderParamType::Mat3:
            return "f32m3x3";
        case ShaderParamType::Mat4:
            return "f32m4x4";
    }
    return "f32";
}

eastl::string BuildMaterialSchemaConfig(const ShaderReflectionData& reflection) {
    ConfigArray params;

    for (const ShaderReflectedBinding& binding : reflection.bindings) {
        if (!binding.IsMaterial()) {
            continue;
        }

        if (binding.kind == ShaderResourceKind::UniformBuffer) {
            for (const ShaderReflectedParam& param : binding.params) {
                // Выравнивающие поля `_padding` не являются параметрами материала.
                if (!param.name.empty() && param.name.front() == '_') {
                    continue;
                }

                ConfigObject item;
                item.fields.push_back(MakeStringField("name", "string", param.name));
                item.fields.push_back(MakeStringField("type", "string", ShaderParamTypeToConfigType(param.type)));
                item.fields.push_back(MakeStringField("kind", "string", "uniform"));
                item.fields.push_back(MakeStringField("buffer", "string", binding.name));
                item.fields.push_back(MakeU32Field("binding", "u32", binding.binding));
                item.fields.push_back(MakeU32Field("offset", "u32", param.offset));
                item.fields.push_back(MakeU32Field("size", "u32", param.size));

                params.elements.push_back(MakeObjectElement("MaterialParam", eastl::move(item)));
            }
            continue;
        }

        ConfigObject item;
        item.fields.push_back(MakeStringField("name", "string", binding.name));
        item.fields.push_back(MakeStringField("type", "string", "u64"));
        item.fields.push_back(MakeStringField("kind", "string", "texture"));
        item.fields.push_back(MakeU32Field("binding", "u32", binding.binding));
        params.elements.push_back(MakeObjectElement("MaterialParam", eastl::move(item)));
    }

    ConfigObject root;
    ConfigField params_field;
    params_field.name = "params";
    params_field.type_name = "MaterialParam[]";
    params_field.value = eastl::make_unique<ConfigArray>(eastl::move(params));
    root.fields.push_back(eastl::move(params_field));

    return tryengine::resources::WriteConfig(root);
}

}  // namespace tryengine::graphics
