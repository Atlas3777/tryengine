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
    explicit MaterialLoader(resources::ResourceManager& rm) : resource_manager_(rm) {}

    async::Task<Material> Parse(const eastl::span<const uint8_t> data) const {
        auto asset_data_opt = tryeditor::Deserialize<MaterialAsset>(data);

        if (!asset_data_opt.has_value())
            co_return core::Error("Deserialize MaterialAsset failed");

        MaterialAsset asset_data = *asset_data_opt;

        const auto shader_res = co_await resource_manager_.GetAsync<Shader>(asset_data.shader_asset_id);

        if (!shader_res.has_value())
            co_return LogAndMakeError("Shader resource loading error {}", asset_data.shader_asset_id);

        Material material(*shader_res);

        for (auto const& [name, values] : asset_data.scalar_params) {
            material.SetParamRaw(name, values.data(), values.size() * sizeof(float));
        }

        for (auto const& [name, tex_bind] : asset_data.texture_params) {
            if (tex_bind.texture_id == 0)
                LogCritical("material texture id = 0");

            const auto texture_res = co_await resource_manager_.GetAsync<TextureSampler>(tex_bind.texture_id);

            if (!texture_res.has_value())
                co_return LogAndMakeError("Texture resource loading error {}", asset_data.shader_asset_id);

            material.SetTexture(*texture_res);
        }

        if (material.textures.empty())
            LogWarn("Material loaded with 0 textures");

        co_return material;
    }

private:
    resources::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics