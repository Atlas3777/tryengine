#pragma once
#include <SDL3/SDL_log.h>

#include "engine/graphics/MaterialSystem.hpp"
#include "engine/resources/MaterialAssetData.hpp"

namespace tryengine::graphics {

class MaterialLoader {
public:
    using result_type = std::shared_ptr<Material>;

    explicit MaterialLoader(core::ResourceManager& rm) : resource_manager_(rm) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open())
            return nullptr;

        resources::MaterialAssetData asset_data;
        try {
            cereal::BinaryInputArchive archive(is);
            archive(asset_data);
        } catch (...) {
            return nullptr;
        }

        // 1. Получаем рантайм-шейдер через ResourceManager
        auto shader_res = resource_manager_.Get<Shader>(asset_data.shader_asset_id);
        if (!shader_res) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MaterialLoader: Shader not found!");
            return nullptr;
        }

        // 2. Создаем материал и привязываем шейдер (это создаст буфер нужного размера)
        auto material = std::make_shared<Material>();
        material->Attach(&*shader_res);

        // 3. Накатываем значения из ассета поверх дефолтных
        for (auto const& [name, values] : asset_data.scalar_params) {
            material->SetParamRaw(name, values.data(), values.size() * sizeof(float));
        }

        // 4. Загружаем текстуры
        for (auto const& [name, tex_id] : asset_data.texture_params) {
            if (tex_id == 0)
                continue;

            // Здесь нужен твой метод загрузки текстур и получения сэмплера
            auto tex_res = resource_manager_.Get<Texture>(tex_id);
            if (tex_res) {
                material->SetTexture(name, tex_res);
            }
        }

        return material;
    }

private:
    core::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics