#include "editor/Spawner.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace tryeditor {

void Spawner::Spawn(entt::registry& reg, uint64_t asset_id) const {
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
        const auto& node_data = asset_map.nodes[i];
        entt::entity entity = entities[i];

        // Имя и Трансформ
        reg.emplace<tryengine::Tag>(entity, node_data.name.empty() ? "New Node" : node_data.name);
        reg.emplace<tryengine::Transform>(entity, node_data.local_transform);

        auto& rel = reg.get_or_emplace<tryengine::Relationship>(entity);
        entt::entity last_child = entt::null;

        for (int32_t child_idx : node_data.children_indices) {
            if (child_idx >= 0 && child_idx < entities.size()) {
                entt::entity child_entity = entities[child_idx];
                auto& childRel = reg.get_or_emplace<tryengine::Relationship>(child_entity);

                childRel.parent = entity;
                rel.children++;

                // Выстраиваем цепочку сиблингов (next/prev)
                if (last_child == entt::null) {
                    rel.first = child_entity;
                } else {
                    reg.get<tryengine::Relationship>(last_child).next = child_entity;
                    childRel.prev = last_child;
                }
                last_child = child_entity;
            }
        }

        // Рендер компоненты (если есть меш)
        if (node_data.mesh_id != 0) {
            auto mesh_resource = resource_manager_.Get<tryengine::graphics::Mesh>(node_data.mesh_id);
            reg.emplace<tryengine::MeshFilter>(entity, mesh_resource, node_data.mesh_id);

            auto material_resource = resource_manager_.Get<tryengine::graphics::Material>(node_data.material_id);
            reg.emplace<tryengine::MeshRenderer>(entity, material_resource, node_data.material_id);
        }
    }
}
}  // namespace tryeditor