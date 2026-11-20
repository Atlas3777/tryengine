#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace tryengine::resources {

struct AssetPak {
    eastl::string name;
    eastl::string scene;  // Полностью сериализованная сцена.
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> shaders;
    eastl::vector<uint64_t> textures;
    eastl::vector<uint64_t> mats;

    eastl::vector<eastl::vector<uint8_t>> meshes_data;
    eastl::vector<eastl::vector<uint8_t>> shaders_data;
    eastl::vector<eastl::vector<uint8_t>> textures_data;
    eastl::vector<eastl::vector<uint8_t>> mats_data;

    // Content-файлы (prefabs, scenes, data): id — PathId(путь), данные — сам текст.
    // content_paths дублирует пути только для манифеста: в паке листинга нет.
    eastl::vector<uint64_t> contents;
    eastl::vector<eastl::vector<uint8_t>> contents_data;
    eastl::vector<eastl::string> content_paths;

    // Стартовая сцена билда; пустая строка — стартовой сцены нет. Pack кладёт её в пак
    // под фиксированным id (kStartupSceneId) вместе с манифестом.
    eastl::string startup_scene;

    eastl::vector<eastl::string> requirement_paks;
};

}  // namespace tryengine::resources