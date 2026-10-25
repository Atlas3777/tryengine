#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "engine/resources/AssetPak.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

struct SceneDTO {
    uint64_t scene_guid;
    const char* name;
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> mats;
};

struct BuildSettings {
    uint64_t first_scene_guid;
    // Добавим че-нить еще позже
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
    tryengine::async::Task<tryengine::resources::AssetPak> CreatePak(
        const SceneDTO& scene,
        const eastl::vector<uint64_t>& shaders,
        const eastl::vector<uint64_t>& textures) const;


    tryengine::resources::ResourceManager& res_m_;
    tryengine::resources::AsyncFileManager& file_m_;
};

}  // namespace tryeditor