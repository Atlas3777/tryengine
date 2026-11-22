#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace tryengine::resources {

struct AssetPak {
    eastl::string name;
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> shaders;
    eastl::vector<uint64_t> textures;
    eastl::vector<uint64_t> mats;
    // Baked FontBinary artifacts (one per .ttf asset). The layout is guid -> bytes like any
    // other artifact, so the pak format itself does not grow a new concept.
    eastl::vector<uint64_t> fonts;

    eastl::vector<eastl::vector<uint8_t>> meshes_data;
    eastl::vector<eastl::vector<uint8_t>> shaders_data;
    eastl::vector<eastl::vector<uint8_t>> textures_data;
    eastl::vector<eastl::vector<uint8_t>> mats_data;
    eastl::vector<eastl::vector<uint8_t>> fonts_data;

    // Content-файлы (prefabs, scenes, data): id — PathId(путь), данные — сам текст.
    // Путь в пак не пишется: runtime считает тот же хеш из авторского пути.
    eastl::vector<uint64_t> contents;
    eastl::vector<eastl::vector<uint8_t>> contents_data;

    eastl::vector<eastl::string> requirement_paks;
};

}  // namespace tryengine::resources