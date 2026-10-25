#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

namespace tryengine::resources {

struct AssetPak {
    eastl::string name;
    eastl::string scene;            // Полностью сериализованная сцена.
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> shaders;
    eastl::vector<uint64_t> textures;
    eastl::vector<uint64_t> mats;

    eastl::vector<eastl::vector<uint8_t>> meshes_data;
    eastl::vector<eastl::vector<uint8_t>> shaders_data;
    eastl::vector<eastl::vector<uint8_t>> textures_data;
    eastl::vector<eastl::vector<uint8_t>> mats_data;


    eastl::vector<eastl::string> requirement_paks;
};

}