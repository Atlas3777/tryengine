#pragma once

#include "engine/async/GlobalExecutors.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/AssetTypes.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryengine::graphics {

class MaterialLoader {
public:
    explicit MaterialLoader(VulkanDevice& device, resources::ResourceManager& rm) : device_(device), resource_manager_(rm) {}

    async::Task<Material> Parse(const eastl::span<const uint8_t> data) const {
        auto asset_data_opt = tryeditor::Deserialize<MaterialAsset>(data);

        if (!asset_data_opt.has_value())
            co_return core::Error("Deserialize MaterialAsset failed");

        MaterialAsset asset_data = *asset_data_opt;

        // 1. Асинхронно загружаем шейдер
        const auto shader_res = co_await resource_manager_.GetAsync<Shader>(asset_data.shader_asset_id);

        if (!shader_res.has_value())
            co_return LogAndMakeError("Shader resource loading error {}", asset_data.shader_asset_id);

        // 2. Создаем материал. Конструктор Material вызовет RebuildLayout(),
        // прочитает рефлексию шейдера, выделит uniform_buffer и соберет param_offsets/cached_ubo_bindings.
        Material material(*shader_res);

        // 3. Заполняем скалярные параметры.
        // Так как SetParamRaw ждет RGTag (или string_view), явно хэшируем имя через RGTagOf:
        for (auto const& [name, values] : asset_data.scalar_params) {
            material.SetParamRaw(
                RGTagOf(name),
                values.data(),
                static_cast<uint32_t>(values.size() * sizeof(float))
            );
        }

        // 4. Привязываем текстуры
        for (auto const& [name, tex_bind] : asset_data.texture_params) {
            if (tex_bind.texture_id == 0) {
                LogWarn("Material '{}': texture binding '{}' has id = 0", asset_data.name.c_str(), name.c_str());
                continue;
            }

            const auto texture_res = co_await resource_manager_.GetAsync<TextureSampler>(tex_bind.texture_id);

            if (!texture_res.has_value())
                co_return LogAndMakeError("Texture resource loading error {}", tex_bind.texture_id); // Исправлен ID в логе

            material.SetTexture(name, *texture_res);
        }

        if (material.textures.empty())
            LogWarn("Material '{}' loaded with 0 textures", asset_data.name.c_str());

        material.InitGpuResources(device_.GetDevice(), device_.GetPhysicalDevice());

        co_return eastl::move(material);
    }

private:
    VulkanDevice& device_;
    resources::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics