// #pragma once
//
// #include "engine/resources/ResourceManager.hpp"
// #include "engine/graphics/Types.hpp"
// #include "engine/resources/MaterialAssetData.hpp"
//
// namespace tryengine::graphics {
//
// class MaterialLoader {
// public:
//     using result_type = std::shared_ptr<Material>;
//
//     explicit MaterialLoader(resources::ResourceManager& rm) : resource_manager_(rm) {}
//
//     result_type operator()(uint64_t id, const std::string& path) const {
//         resources::MaterialAssetData asset_data;
//         auto ec = detail::read_beve_file(asset_data, path);
//         if (ec) {
//             SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MaterialLoader: failed to read material '%s': %s",
//                          path.c_str(), glz::format_error(ec).c_str());
//             return nullptr;
//         }
//
//         // 1. Получаем рантайм-шейдер через ResourceManager
//         auto shader_res = resource_manager_.Get<Shader>(asset_data.shader_asset_id);
//         if (!shader_res) {
//             SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MaterialLoader: Shader not found!");
//             return nullptr;
//         }
//
//         // 2. Создаем материал и привязываем шейдер (это создаст буфер нужного размера)
//         auto material = std::make_shared<Material>();
//         material->Attach(&*shader_res);
//
//         // 3. Накатываем значения из ассета поверх дефолтных
//         for (auto const& [name, values] : asset_data.scalar_params) {
//             material->SetParamRaw(name, values.data(), values.size() * sizeof(float));
//         }
//
//         // 4. Загружаем текстуры
//         for (auto const& [name, tex_id] : asset_data.texture_params) {
//             if (tex_id == 0)
//                 continue;
//
//             // Здесь нужен твой метод загрузки текстур и получения сэмплера
//             auto tex_res = resource_manager_.Get<Texture>(tex_id);
//             if (tex_res) {
//                 material->SetTexture(name, tex_res);
//             }
//         }
//
//         return material;
//     }
//
// private:
//     resources::ResourceManager& resource_manager_;
// };
//
// }  // namespace tryengine::graphics