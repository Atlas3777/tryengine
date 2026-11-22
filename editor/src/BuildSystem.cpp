#include "editor/BuildSystem.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/hash_set.h>
#include <EASTL/sort.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <type_traits>

#include "../../engine_source/resources/include/engine/resources/binary/AssetPakBinary.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/resources/AssetBytes.hpp"
#include "engine/resources/AssetPak.hpp"
#include "engine/resources/ContentAssets.hpp"

namespace tryeditor {

namespace {

bool StartsWith(eastl::string_view value, eastl::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(eastl::string_view value, eastl::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

void AppendUnique(eastl::vector<uint64_t>& destination, eastl::vector<uint64_t> source) {
    destination.insert(destination.end(), source.begin(), source.end());
    eastl::sort(destination.begin(), destination.end());
    destination.erase(eastl::unique(destination.begin(), destination.end()), destination.end());
}

struct ContentFile {
    eastl::string path;
    uint64_t id = 0;
    eastl::vector<uint8_t> data;
};

}  // namespace

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
    for (const auto& kv : ref_count)
        if (kv.second > 1)
            shared.push_back(kv.first);
    eastl::sort(shared.begin(), shared.end());

    // 3. Чистим каждый список.
    auto is_shared = [&shared](uint64_t g) { return eastl::binary_search(shared.begin(), shared.end(), g); };
    for (auto* list : per_scene_lists)
        list->erase(eastl::remove_if(list->begin(), list->end(), is_shared), list->end());

    return shared;
}

tryengine::async::Task<void> BuildSystem::Build(BuildSettings settings, eastl::span<SceneDTO> scenes,
                                                const char* build_directory) {
    LogInfo("BuildSystem::Build enter");

    // Контейнер для хранения хэндлов. Используем type-erasure, чтобы не знать точный тип хэндла.
    // Когда active_handles выйдет из скоупа в конце билда, кастомный делитер корректно удалит
    // скопированные хэндлы, что автоматически отпустит ресурсы из кэша ResourceManager.
    eastl::vector<eastl::unique_ptr<void, void (*)(void*)>> active_handles;
    auto store_handle = [&](auto&& handle_opt) {
        if (handle_opt.has_value()) {
            using H = std::decay_t<decltype(*handle_opt)>;
            active_handles.push_back(eastl::unique_ptr<void, void (*)(void*)>(
                new H(*handle_opt), [](void* p) { delete static_cast<H*>(p); }));
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
            // Загружаем данные материала (ConfigFormat), чтобы достать GUID'ы шейдера и текстур
            auto mat_res = co_await res_m_.GetAsync<tryengine::graphics::MaterialConfig>(mat);
            store_handle(mat_res);  // Сохраняем хэндл, чтобы ресурс не выгрузился из кэша

            if (mat_res.has_value()) {
                const auto& asset_data = *mat_res;
                if (asset_data->shader != 0)
                    per_scene_shaders[i].push_back(asset_data->shader);
                for (const auto& field : asset_data->textures.fields)
                    if (const auto* texture_id = eastl::get_if<uint64_t>(&field.value); texture_id && *texture_id != 0)
                        per_scene_textures[i].push_back(*texture_id);
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
    for (auto& mat_guid : common_mats)
        LogInfo("COMMON GUID MAT {}", mat_guid);
    for (auto& mesh_guid : common_meshes)
        LogInfo("COMMON MESH {}", mesh_guid);
    for (auto& shader_guid : common_shaders)
        LogInfo("COMMON SHADER {}", shader_guid);
    for (auto& tex_guid : common_textures)
        LogInfo("COMMON TEXTURE {}", tex_guid);

    SceneDTO common_dto{};
    common_dto.meshes = eastl::move(common_meshes);
    common_dto.mats = eastl::move(common_mats);
    common_dto.name = "common_dto";

    // 3. Собираем content до common-пака: префабы должны уехать туда вместе со своими
    // мешами и материалами, иначе Prefab::Spawn в игре получит текст без ресурсов.
    eastl::vector<ContentFile> content_files;
    const auto content_paths = res_m_.GetAssetRegistry().AuthoritativePaths();
    for (const auto& path : content_paths) {
        const bool is_prefab = StartsWith(path, "game/content/") && EndsWith(path, ".prefab");
        const bool is_data = StartsWith(path, "game/data/");
        if (!is_prefab && !is_data)
            continue;

        const uint64_t id = tryengine::core::PathId(path);
        auto bytes = co_await tryengine::resources::ReadAssetBytes(file_m_, res_m_.GetAssetRegistry(), id);
        if (!bytes.has_value()) {
            LogError("Content file '{}' is not readable; skipped", path.c_str());
            continue;
        }

        ContentFile file;
        file.path = path;
        file.id = id;
        file.data = eastl::move(*bytes);

        if (is_prefab) {
            const eastl::string_view text(reinterpret_cast<const char*>(file.data.data()), file.data.size());
            tryengine::resources::ContentDependencies deps;
            tryengine::resources::CollectPrefabDependencies(text, res_m_.GetAssetRegistry(), deps);
            AppendUnique(common_dto.meshes, eastl::move(deps.meshes));
            AppendUnique(common_dto.mats, eastl::move(deps.materials));
        } else if (is_data && EndsWith(path, ".mat")) {
            // A material may be requested at runtime by path even if no scene/prefab
            // references it; its shader and textures belong in common as well.
            eastl::vector<uint64_t> data_mat;
            data_mat.push_back(id);
            AppendUnique(common_dto.mats, eastl::move(data_mat));
        }

        content_files.push_back(eastl::move(file));
    }

    // Материалы (общие и из префабов) тянут за собой шейдеры и текстуры.
    for (uint64_t mat : common_dto.mats) {
        auto mat_res = co_await res_m_.GetAsync<tryengine::graphics::MaterialConfig>(mat);
        if (!mat_res.has_value()) {
            LogError("Common material {} is not readable; skipped", mat);
            continue;
        }
        const auto& asset_data = *mat_res;
        if (asset_data->shader != 0)
            common_shaders.push_back(asset_data->shader);
        for (const auto& field : asset_data->textures.fields)
            if (const auto* texture_id = eastl::get_if<uint64_t>(&field.value); texture_id && *texture_id != 0)
                common_textures.push_back(*texture_id);
    }
    eastl::sort(common_shaders.begin(), common_shaders.end());
    common_shaders.erase(eastl::unique(common_shaders.begin(), common_shaders.end()), common_shaders.end());
    eastl::sort(common_textures.begin(), common_textures.end());
    common_textures.erase(eastl::unique(common_textures.begin(), common_textures.end()), common_textures.end());

    eastl::vector<tryengine::resources::AssetPak> asset_paks;

    // 4. Пакуем сцены вместе с их уникальными зависимостями
    for (size_t i = 0; i < scenes.size(); ++i) {
        auto o = co_await CreatePak(scenes[i], per_scene_shaders[i], per_scene_textures[i], {});
        if (!o.has_value())
            co_return LogAndMakeError("Create Pak Is Failed");
        // Общие ресурсы и content лежат в common; PakManager подтянет его по зависимостям.
        o->requirement_paks.push_back("content/common_dto.pak");
        asset_paks.emplace_back(eastl::move(*o));
    }

    // 5. Пакуем общие ресурсы: сцены делят их между собой, а префабы могут понадобиться
    // в любой момент, поэтому common несёт все prefab/data и их зависимости.
    auto common_result = co_await CreatePak(common_dto, common_shaders, common_textures, settings.fonts);
    if (!common_result.has_value())
        co_return LogAndMakeError("Create Pak Is Failed");

    tryengine::resources::AssetPak common_pak = eastl::move(*common_result);

    for (auto& file : content_files) {
        common_pak.contents.push_back(file.id);
        common_pak.contents_data.push_back(eastl::move(file.data));
    }

    LogInfo("Common pak: {} content file(s), {} meshes, {} materials, {} fonts", common_pak.contents.size(),
            common_dto.meshes.size(), common_dto.mats.size(), common_pak.fonts.size());
    asset_paks.emplace_back(eastl::move(common_pak));

    LogInfo("Начали писать в папку {}", build_directory);
    for (auto& asset_pak : asset_paks) {
        auto pak_binary = tryengine::resources::AssetPakBinary::Pack(asset_pak);
        auto path = tryengine::fmt::format("{}/content/{}.pak", build_directory, asset_pak.name);
        tryengine::resources::FileHandle h = file_m_.WriteChunkAsync(path, pak_binary);
        co_await h;
    }
    LogInfo("Закончили писать в папку");
    co_return {};
}

tryengine::async::Task<tryengine::resources::AssetPak> BuildSystem::CreatePak(
    const SceneDTO& scene, const eastl::vector<uint64_t>& shaders, const eastl::vector<uint64_t>& textures,
    const eastl::vector<uint64_t>& fonts) const {
    LogInfo("CreatePak enter, scene_name={}", scene.name);

    tryengine::resources::AssetPak pak;
    pak.name = scene.name;

    auto& registry = res_m_.GetAssetRegistry();

    // Текст сцены — такой же content-файл, как префаб: id = PathId(путь), данные — текст.
    // Читается напрямую, без кеша: сцена могла быть сохранена в этой же сессии редактора,
    // и закешированная копия запаковала бы прошлую ревизию.
    if (!scene.path.empty()) {
        const uint64_t scene_id = tryengine::core::PathId(scene.path);
        auto bytes = co_await tryengine::resources::ReadAssetBytes(file_m_, registry, scene_id);
        if (bytes.has_value()) {
            pak.contents.push_back(scene_id);
            pak.contents_data.push_back(eastl::move(*bytes));
        } else {
            LogError("Scene text '{}' is not readable", scene.path.c_str());
        }
    }

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

    // Baked fonts (.font artifacts). Requested by guid at runtime like any resource.
    for (uint64_t font : fonts) {
        auto o = registry.GetAssetLocation(font);
        if (!o.has_value()) {
            LogError("Font with guid {} not found; import the .ttf first", font);
            continue;
        }
        if (const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&*o)) {
            tryengine::resources::FileHandle stat_handle = file_m_.GetStatAsync(loose->path);
            co_await stat_handle;

            auto data_handle = file_m_.ReadChunkAsync(loose->path, 0, stat_handle.GetFileSize());
            co_await data_handle;

            pak.fonts_data.emplace_back(eastl::move(data_handle.TakeData()));
            pak.fonts.emplace_back(font);
        }
    }

    co_return pak;
}

}  // namespace tryeditor