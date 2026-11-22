#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "engine/resources/AssetPak.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

struct SceneDTO {
    uint64_t scene_guid;
    eastl::string name;
    // Авторский путь (game/content/...): одновременно id сцены (PathId) и ключ для
    // чтения текста через общий asset API.
    eastl::string path;
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> mats;
};

struct BuildSettings {
    uint64_t first_scene_guid;
    // Assets that must ride in the common pak although no scene or prefab references them.
    // Today this is the baked UI font and the textures the HUD draws by guid (UiSystem config);
    // CreateBuild fills both in.
    eastl::vector<uint64_t> fonts;
    eastl::vector<uint64_t> ui_textures;
};

class BuildSystem {
public:
    explicit BuildSystem(tryengine::resources::ResourceManager& resource_m,
                         tryengine::resources::AsyncFileManager& file_m)
        : res_m_(resource_m), file_m_(file_m) {}

    tryengine::async::Task<void> Build(BuildSettings settings, eastl::span<SceneDTO> scenes,
                                       const char* build_directory);

private:
    eastl::vector<uint64_t> ExtractAndRemoveShared(eastl::span<eastl::vector<uint64_t>*> per_scene_lists);

    // Изменили сигнатуру: передаем зависимости явно, не раздувая SceneDTO
    tryengine::async::Task<tryengine::resources::AssetPak> CreatePak(const SceneDTO& scene,
                                                                     const eastl::vector<uint64_t>& shaders,
                                                                     const eastl::vector<uint64_t>& textures,
                                                                     const eastl::vector<uint64_t>& fonts) const;

    tryengine::resources::ResourceManager& res_m_;
    tryengine::resources::AsyncFileManager& file_m_;
};

}  // namespace tryeditor