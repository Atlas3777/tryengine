#include "editor/BuildSystem.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unique_ptr.h>
#include <type_traits>

#include "engine/resources/AssetPak.hpp"
#include "engine/resources/AssetPakBinary.hpp"
#include "engine/graphics/AssetTypes.hpp"

namespace tryeditor {

eastl::vector<uint64_t> BuildSystem::ExtractAndRemoveShared(eastl::span<eastl::vector<uint64_t>*> per_scene_lists) {
    eastl::unordered_map<uint64_t, uint32_t> ref_count;
    size_t hint = 0;
    for (auto* list : per_scene_lists)
        hint += list->size();
    ref_count.reserve(hint);

    for (auto* list : per_scene_lists) {
        // ВАЖНО: Уникализируем ресурсы в пределах ОДНОЙ сцены.
        // Иначе, если 2 материала на сцене А используют одну текстуру, ref_count станет 2,
        // и текстура ошибочно уедет в common пак.
        eastl::sort(list->begin(), list->end());
        list->erase(eastl::unique(list->begin(), list->end()), list->end());

        for (uint64_t g : *list)
            ++ref_count[g];
    }

    // 2. Отбираем общие и сортируем (детерминированный билд + binary_search ниже).
    eastl::vector<uint64_t> shared;
    shared.reserve(ref_count.size());
    for (const auto& kv : ref_count) {
        if (kv.second > 1)
            shared.push_back(kv.first);
    }
    eastl::sort(shared.begin(), shared.end());

    // 3. Чистим каждый список.
    auto is_shared = [&shared](uint64_t g) { return eastl::binary_search(shared.begin(), shared.end(), g); };
    for (auto* list : per_scene_lists) {
        list->erase(eastl::remove_if(list->begin(), list->end(), is_shared), list->end());
    }

    return shared;
}

tryengine::async::Task<void> BuildSystem::Build(BuildSettings settings, eastl::span<SceneDTO> scenes,
                                                const char* build_directory) {
    LogInfo("BuildSystem::Build enter");

    // Контейнер для хранения хэндлов. Используем type-erasure, чтобы не знать точный тип хэндла.
    // Когда active_handles выйдет из скоупа в конце билда, кастомный делитер корректно удалит
    // скопированные хэндлы, что автоматически отпустит ресурсы из кэша ResourceManager.
    eastl::vector<eastl::unique_ptr<void, void(*)(void*)>> active_handles;
    auto store_handle = [&](auto&& handle_opt) {
        if (handle_opt.has_value()) {
            using H = std::decay_t<decltype(*handle_opt)>;
            active_handles.push_back(eastl::unique_ptr<void, void(*)(void*)>(
                new H(*handle_opt),
                [](void* p){ delete static_cast<H*>(p); }
            ));
        }
    };

    // Внутренние списки зависимостей для каждой сцены (не лезем в SceneDTO)
    eastl::vector<eastl::vector<uint64_t>> per_scene_shaders(scenes.size());
    eastl::vector<eastl::vector<uint64_t>> per_scene_textures(scenes.size());

    // 1. Резолвим зависимости материалов через ResourceManager
    LogInfo("Resolving material dependencies...");
    for (size_t i = 0; i < scenes.size(); ++i) {
        const auto& s = scenes[i];
        for (uint64_t mat : s.mats) {
            // Загружаем MaterialAsset, чтобы достать GUID'ы шейдеров и текстур
            auto mat_res = co_await res_m_.GetAsync<tryengine::graphics::MaterialAsset>(mat);
            store_handle(mat_res); // Сохраняем хэндл, чтобы ресурс не выгрузился из кэша

            if (mat_res.has_value()) {
                const auto& asset_data = *mat_res;
                if (asset_data->shader_asset_id != 0) {
                    per_scene_shaders[i].push_back(asset_data->shader_asset_id);
                }
                for (const auto& [name, tex_bind] : asset_data->texture_params) {
                    if (tex_bind.texture_id != 0) {
                        per_scene_textures[i].push_back(tex_bind.texture_id);
                    }
                }
            }
        }
    }
    LogInfo("Dependencies resolved.");

    // 2. Собираем указатели на все списки для дедупликации
    eastl::vector<eastl::vector<uint64_t>*> mesh_lists;
    eastl::vector<eastl::vector<uint64_t>*> mat_lists;
    eastl::vector<eastl::vector<uint64_t>*> shader_lists;
    eastl::vector<eastl::vector<uint64_t>*> texture_lists;

    mesh_lists.reserve(scenes.size());
    mat_lists.reserve(scenes.size());
    shader_lists.reserve(scenes.size());
    texture_lists.reserve(scenes.size());

    for (size_t i = 0; i < scenes.size(); ++i) {
        mesh_lists.push_back(&scenes[i].meshes);
        mat_lists.push_back(&scenes[i].mats);
        shader_lists.push_back(&per_scene_shaders[i]);
        texture_lists.push_back(&per_scene_textures[i]);
    }

    auto common_meshes = ExtractAndRemoveShared(eastl::span(mesh_lists));
    auto common_mats = ExtractAndRemoveShared(eastl::span(mat_lists));
    auto common_shaders = ExtractAndRemoveShared(eastl::span(shader_lists));
    auto common_textures = ExtractAndRemoveShared(eastl::span(texture_lists));

    LogInfo("ExtractAndRemoveShared end");
    for (auto& mat_guid : common_mats) LogInfo("COMMON GUID MAT {}", mat_guid);
    for (auto& mesh_guid : common_meshes) LogInfo("COMMON MESH {}", mesh_guid);
    for (auto& shader_guid : common_shaders) LogInfo("COMMON SHADER {}", shader_guid);
    for (auto& tex_guid : common_textures) LogInfo("COMMON TEXTURE {}", tex_guid);

    SceneDTO common_dto;
    common_dto.meshes = eastl::move(common_meshes);
    common_dto.mats = eastl::move(common_mats);
    common_dto.name = "common_dto";

    eastl::vector<tryengine::resources::AssetPak> asset_paks;

    // 3. Пакуем сцены вместе с их уникальными зависимостями
    for (size_t i = 0; i < scenes.size(); ++i) {
        auto o = co_await CreatePak(scenes[i], per_scene_shaders[i], per_scene_textures[i]);
        if (!o.has_value())
            co_return LogAndMakeError("Create Pak Is Failed");
        asset_paks.emplace_back(eastl::move(*o));
    }

    // 4. Пакуем общие ресурсы
    auto a = co_await CreatePak(common_dto, common_shaders, common_textures);
    if (!a.has_value())
        co_return LogAndMakeError("Create Pak Is Failed");
    asset_paks.emplace_back(eastl::move(*a));

    LogInfo("Начали писать в папку");
    for (auto& asset_pak : asset_paks) {
        auto pak_binary = tryengine::resources::AssetPakBinary::Pack(asset_pak);
        auto path = tryengine::fmt::format("{}/content/{}.pak", build_directory, asset_pak.name);
        tryengine::resources::FileHandle h = file_m_.WriteChunkAsync(path, pak_binary);
        co_await h;
    }
    auto path = tryengine::fmt::format("{}/content/{}", build_directory, "setting");
    auto data = tryengine::fmt::format("Какие-то данные");
    eastl::span view{reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    tryengine::resources::FileHandle h = file_m_.WriteChunkAsync(path, view);
    co_await h;

    // Здесь active_handles уничтожается, вызывая делитеры и отпуская кэш ResourceManager
    LogInfo("Закончили писать в папку");
    co_return {};
}

tryengine::async::Task<tryengine::resources::AssetPak> BuildSystem::CreatePak(
    const SceneDTO& scene,
    const eastl::vector<uint64_t>& shaders,
    const eastl::vector<uint64_t>& textures) const
{
    LogInfo("CreatePak enter, scene_name={}", scene.name);

    tryengine::resources::AssetPak pak;
    pak.name = scene.name;

    auto& registry = res_m_.GetAssetRegistry();

    for (uint64_t mesh : scene.meshes) {
        auto o = registry.GetAssetLocation(mesh);
        if (!o.has_value()) {
            LogError("File with guid {} not found", mesh);
            continue;
        }
        if (const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&*o)) {
            tryengine::resources::FileHandle stat_handle = file_m_.GetStatAsync(loose->path);
            co_await stat_handle;

            auto data_handle = file_m_.ReadChunkAsync(loose->path, 0, stat_handle.GetFileSize());
            co_await data_handle;

            pak.meshes_data.emplace_back(eastl::move(data_handle.TakeData()));
            pak.meshes.emplace_back(mesh);
        }
    }

    for (uint64_t mat : scene.mats) {
        auto o = registry.GetAssetLocation(mat);
        if (!o.has_value()) {
            LogError("File with guid {} not found", mat);
            continue;
        }
        auto location = *o;
        if (const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&location)) {
            tryengine::resources::FileHandle stat_handle = file_m_.GetStatAsync(loose->path);
            co_await stat_handle;

            auto data_handle = file_m_.ReadChunkAsync(loose->path, 0, stat_handle.GetFileSize());
            co_await data_handle;

            pak.mats_data.emplace_back(eastl::move(data_handle.TakeData()));
            pak.mats.emplace_back(mat);
        }
    }

    for (uint64_t shader : shaders) {
        auto o = registry.GetAssetLocation(shader);
        if (!o.has_value()) {
            LogError("File with guid {} not found", shader);
            continue;
        }
        if (const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&*o)) {
            tryengine::resources::FileHandle stat_handle = file_m_.GetStatAsync(loose->path);
            co_await stat_handle;

            auto data_handle = file_m_.ReadChunkAsync(loose->path, 0, stat_handle.GetFileSize());
            co_await data_handle;

            pak.shaders_data.emplace_back(eastl::move(data_handle.TakeData()));
            pak.shaders.emplace_back(shader);
        }
    }

    for (uint64_t tex : textures) {
        auto o = registry.GetAssetLocation(tex);
        if (!o.has_value()) {
            LogError("File with guid {} not found", tex);
            continue;
        }
        if (const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&*o)) {
            tryengine::resources::FileHandle stat_handle = file_m_.GetStatAsync(loose->path);
            co_await stat_handle;

            auto data_handle = file_m_.ReadChunkAsync(loose->path, 0, stat_handle.GetFileSize());
            co_await data_handle;

            pak.textures_data.emplace_back(eastl::move(data_handle.TakeData()));
            pak.textures.emplace_back(tex);
        }
    }

    co_return pak;
}

}  // namespace tryeditor