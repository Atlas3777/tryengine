#pragma once

#include "engine/async/GlobalExecutors.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryengine::graphics {

namespace material_detail {

// Имя параметра ищется в UBO материала (set 1), остальные биндинги не материал.
inline const ShaderReflectedParam* FindUniformParam(const ShaderReflectionData& reflection, eastl::string_view name) {
    for (const ShaderReflectedBinding& binding : reflection.bindings) {
        if (!binding.IsMaterial() || binding.kind != ShaderResourceKind::UniformBuffer) {
            continue;
        }
        if (const ShaderReflectedParam* param = binding.FindParam(name)) {
            return param;
        }
    }
    return nullptr;
}

inline bool HasTextureBinding(const ShaderReflectionData& reflection, eastl::string_view name) {
    for (const ShaderReflectedBinding& binding : reflection.bindings) {
        if (binding.IsMaterial() && binding.kind == ShaderResourceKind::SampledTexture && binding.name == name) {
            return true;
        }
    }
    return false;
}

inline bool GetParamBytes(const resources::ConfigValue& value, ShaderParamType expected, const void*& data,
                          uint32_t& size) {
    switch (expected) {
        case ShaderParamType::Float:
            if (const auto* v = eastl::get_if<float>(&value)) {
                data = v;
                size = sizeof(float);
                return true;
            }
            return false;
        case ShaderParamType::Int:
            if (const auto* v = eastl::get_if<int32_t>(&value)) {
                data = v;
                size = sizeof(int32_t);
                return true;
            }
            if (const auto* v = eastl::get_if<uint32_t>(&value)) {
                data = v;
                size = sizeof(uint32_t);
                return true;
            }
            return false;
        case ShaderParamType::Vec2:
            if (const auto* v = eastl::get_if<resources::ConfigFloat2>(&value)) {
                data = v;
                size = sizeof(resources::ConfigFloat2);
                return true;
            }
            return false;
        case ShaderParamType::Vec3:
            if (const auto* v = eastl::get_if<resources::ConfigFloat3>(&value)) {
                data = v;
                size = sizeof(resources::ConfigFloat3);
                return true;
            }
            return false;
        case ShaderParamType::Vec4:
            if (const auto* v = eastl::get_if<resources::ConfigFloat4>(&value)) {
                data = v;
                size = sizeof(resources::ConfigFloat4);
                return true;
            }
            return false;
        case ShaderParamType::Mat3:
            if (const auto* v = eastl::get_if<resources::ConfigFloat3x3>(&value)) {
                data = v;
                size = sizeof(resources::ConfigFloat3x3);
                return true;
            }
            return false;
        case ShaderParamType::Mat4:
            if (const auto* v = eastl::get_if<resources::ConfigFloat4x4>(&value)) {
                data = v;
                size = sizeof(resources::ConfigFloat4x4);
                return true;
            }
            return false;
    }
    return false;
}

inline bool GetTextureId(const resources::ConfigValue& value, uint64_t& out) {
    if (const auto* v = eastl::get_if<uint64_t>(&value)) {
        out = *v;
        return true;
    }
    if (const auto* v = eastl::get_if<int64_t>(&value); v && *v >= 0) {
        out = static_cast<uint64_t>(*v);
        return true;
    }
    if (const auto* v = eastl::get_if<uint32_t>(&value)) {
        out = *v;
        return true;
    }
    return false;
}

}  // namespace material_detail

// Применяет uniform-параметры конфига к уже созданному материалу. Общая точка для
// загрузчика и для live-правки из редактора: GPU-ресурсы не пересоздаются, значения
// пишутся прямо в замапленный UBO материала.
inline void ApplyMaterialParams(const MaterialConfig& config, Material& material) {
    for (const resources::ConfigField& field : config.params.fields) {
        if (!field.name.empty() && field.name.front() == '_') {
            continue;
        }

        const ShaderReflectedParam* param = material_detail::FindUniformParam(material.shader->reflection, field.name);
        if (!param) {
            LogWarn("Material '{}': parameter '{}' is not declared by shader {}", config.name.c_str(),
                    field.name.c_str(), config.shader);
            continue;
        }

        const void* param_data = nullptr;
        uint32_t param_size = 0;
        if (!material_detail::GetParamBytes(field.value, param->type, param_data, param_size)) {
            LogWarn("Material '{}': parameter '{}' type '{}' does not match shader type '{}'", config.name.c_str(),
                    field.name.c_str(), field.type_name.c_str(), ShaderParamTypeToConfigType(param->type));
            continue;
        }

        material.SetParamRaw(RGTagOf(field.name), param_data, param_size);
    }
}

// Live-правка из редактора: конфиг применяется к уже загруженному материалу, GPU-ресурсы
// не пересоздаются. Текстуры перепривязываются только если они уже лежат в кеше.
inline bool ApplyMaterialConfigLive(resources::ResourceManager& rm, uint64_t material_guid,
                                    const MaterialConfig& config) {
    Material* material = rm.GetPointerExplicit<Material>(material_guid);
    if (!material) {
        return false;
    }

    ApplyMaterialParams(config, *material);

    for (const resources::ConfigField& field : config.textures.fields) {
        if (!material_detail::HasTextureBinding(material->shader->reflection, field.name)) {
            continue;
        }

        uint64_t texture_id = 0;
        if (!material_detail::GetTextureId(field.value, texture_id) || texture_id == 0) {
            continue;
        }

        auto texture = rm.Get<TextureSampler>(texture_id);
        if (!texture.has_value()) {
            LogWarn("Material '{}': texture '{}' is not loaded yet, live rebind skipped", config.name.c_str(),
                    field.name.c_str());
            continue;
        }
        material->SetTexture(field.name, *texture);
    }
    return true;
}

// Единственный формат материала — ConfigFormat (`MaterialConfig`). Отдельной ветки для
// JSON нет: и материал из data/, и суб-ассет glTF описываются одним документом.
class MaterialLoader {
public:
    explicit MaterialLoader(VulkanDevice& device, resources::ResourceManager& rm)
        : device_(device), resource_manager_(rm) {}

    async::Task<Material> Parse(const eastl::span<const uint8_t> data) const {
        auto config_result = ParseMaterialConfig(data);
        if (!config_result.has_value()) {
            co_return LogAndMakeError("Material config: {}", config_result.error());
        }

        MaterialConfig config = eastl::move(*config_result);

        // 1. Асинхронно загружаем шейдер
        const auto shader_res = co_await resource_manager_.GetAsync<Shader>(config.shader);

        if (!shader_res.has_value())
            co_return LogAndMakeError("Material '{}': shader resource loading error {}", config.name.c_str(),
                                      config.shader);

        // 2. Создаем материал и сразу его GPU-ресурсы: SetParamRaw пишет значения прямо
        // в замапленный uniform buffer, до InitGpuResources они молча теряются.
        Material material(*shader_res);
        material.InitGpuResources(device_.GetDevice(), device_.GetPhysicalDevice());

        // 3. Значения uniform-параметров (set 1).
        ApplyMaterialParams(config, material);

        // 4. Привязываем текстуры: имя поля ищется среди сэмплеров материала (set 1).
        for (const resources::ConfigField& field : config.textures.fields) {
            if (!material_detail::HasTextureBinding((*shader_res)->reflection, field.name)) {
                LogWarn("Material '{}': texture '{}' is not declared by shader {}", config.name.c_str(),
                        field.name.c_str(), config.shader);
                continue;
            }

            uint64_t texture_id = 0;
            if (!material_detail::GetTextureId(field.value, texture_id)) {
                LogWarn("Material '{}': texture '{}' must be a u64 id", config.name.c_str(), field.name.c_str());
                continue;
            }
            if (texture_id == 0) {
                continue;
            }

            const auto texture_res = co_await resource_manager_.GetAsync<TextureSampler>(texture_id);

            if (!texture_res.has_value())
                co_return LogAndMakeError("Material '{}': texture '{}' loading error {}", config.name.c_str(),
                                          field.name.c_str(), texture_id);

            material.SetTexture(field.name, *texture_res);
        }

        if (material.textures.empty())
            LogWarn("Material '{}' loaded with 0 textures", config.name.c_str());

        co_return eastl::move(material);
    }

    VulkanDevice& device_;
    resources::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics
