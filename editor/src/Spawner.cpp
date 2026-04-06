#include "editor/Spawner.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace editor {

void Spawner::Spawn(entt::registry& reg, uint64_t asset_id) {
    auto path = import_system_.GetHierarchyPath(asset_id);
    std::cout << "[Spawner] Opening hierarchy: " << path << std::endl;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Spawner] Could not open file!" << std::endl;
        return;
    }

    cereal::JSONInputArchive archive(file);

    ModelAssetMap map;
    archive(cereal::make_nvp("asset_map", map));

    const auto& asset_map = map;
    std::vector<entt::entity> entities(asset_map.nodes.size());

    for (size_t i = 0; i < asset_map.nodes.size(); ++i) {
        entities[i] = reg.create();
    }

    // 2. Итерируемся по данным и настраиваем компоненты
    for (size_t i = 0; i < asset_map.nodes.size(); ++i) {
        const auto& nodeData = asset_map.nodes[i];
        entt::entity entity = entities[i];

        // Имя и Трансформ
        reg.emplace<engine::Tag>(entity, nodeData.name.empty() ? "New Node" : nodeData.name);
        reg.emplace<engine::Transform>(entity, nodeData.local_transform);

        auto& rel = reg.get_or_emplace<engine::Relationship>(entity);
        entt::entity last_child = entt::null;

        for (int32_t childIdx : nodeData.children_indices) {
            if (childIdx >= 0 && childIdx < entities.size()) {
                entt::entity childEntity = entities[childIdx];
                auto& childRel = reg.get_or_emplace<engine::Relationship>(childEntity);

                childRel.parent = entity;
                rel.children++;

                // Выстраиваем цепочку сиблингов (next/prev)
                if (last_child == entt::null) {
                    rel.first = childEntity;
                } else {
                    reg.get<engine::Relationship>(last_child).next = childEntity;
                    childRel.prev = last_child;
                }
                last_child = childEntity;
            }
        }

        int w, h, c;
        unsigned char* data = stbi_load("game/assets/test.png", &w, &h, &c, STBI_rgb_alpha);
        if (!data) {
            SDL_Log("Failed to load texture: %s", path.c_str());
            // return nullptr;
        }

        engine::graphics::Texture* tex = new engine::graphics::Texture();
        tex->width = static_cast<Uint32>(w);
        tex->height = static_cast<Uint32>(h);
        // tex->path = path;

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = tex->width;
        info.height = tex->height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;

        tex->handle = SDL_CreateGPUTexture(graphics_context_.GetDevice(), &info);

        Uint32 dataSize = tex->width * tex->height * 4;
        SDL_GPUTransferBufferCreateInfo tInfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = dataSize};
        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(graphics_context_.GetDevice(), &tInfo);

        Uint8* map = (Uint8*)SDL_MapGPUTransferBuffer(graphics_context_.GetDevice(), tBuf, false);
        std::memcpy(map, data, dataSize);
        SDL_UnmapGPUTransferBuffer(graphics_context_.GetDevice(), tBuf);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(graphics_context_.GetDevice());
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
        SDL_GPUTextureRegion dst = {tex->handle, 0, 0, 0, 0, 0, tex->width, tex->height, 1};
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);

        SDL_ReleaseGPUTransferBuffer(graphics_context_.GetDevice(), tBuf);
        stbi_image_free(data);


        // Рендер компоненты (если есть меш)
        if (nodeData.mesh_id != 0) {
            // Загружаем меш через ResourceManager
            auto meshRes = resource_manager_.Get<engine::graphics::Mesh>(nodeData.mesh_id);
            reg.emplace<engine::MeshFilter>(entity, meshRes, nodeData.mesh_id);


            engine::graphics::Material* mat = new engine::graphics::Material(); // Выделяем память
            mat->name = "BasicStatic";
            mat->pipeline = render_system_.GetRenderer().GetDefaultPipeline();

            auto* matInstance = new engine::graphics::MaterialInstance(mat);
            matInstance->SetTexture(0, tex->handle, render_system_.GetRenderer().GetCommonSampler());

            reg.emplace<engine::graphics::MeshRenderer>(entity, matInstance);
        }
    }
}
}  // namespace editor