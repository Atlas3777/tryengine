// #include "editor/Spawner.hpp"
//
// #include <unordered_map>
// #include <vector>
//
//
// #include "editor/AssetSourceDatabase.hpp"
// #include "editor/meta/ModelAssetMap.hpp"
// #include "engine/async/GlobalExecutors.hpp"
// #include "engine/core/LogError.hpp"
// #include "engine/core/Random.hpp"
// #include "engine/graphics/RenderCommon.hpp"
// #include "engine/graphics/RuntimeTypes.hpp"
// #include "engine/resources/ResourceManager.hpp"
//
// namespace tryeditor {
//
// tryengine::async::Task<void> Spawn(tryengine::resources::ResourceManager& resource_manager, entt::registry& reg,
//                                    const uint64_t asset_id) {
//
//     tryengine::async::GetCurrentContextInfo();
//
//     auto asset = co_await resource_manager.GetAsync<ModelAssetMap>(asset_id);
//     if (!asset.has_value())
//         co_return tryengine::core::LogError("AssetRegistry::Spawn: asset_id {} not found", asset_id);
//
//
//     auto asset_map = *asset;
//
//     std::unordered_map<uint64_t, tryengine::resources::ResourceHandle<tryengine::graphics::Mesh>> loaded_meshes;
//     std::unordered_map<uint64_t, tryengine::resources::ResourceHandle<tryengine::graphics::Material>> loaded_materials;
//
//     for (const auto& node_data : asset_map->nodes) {
//         if (node_data.mesh_id != 0 && node_data.material_id != 0) {
//             if (!loaded_meshes.contains(node_data.mesh_id)) {
//                 auto mesh_resource = co_await resource_manager.GetAsync<tryengine::graphics::Mesh>(node_data.mesh_id);
//                 if (!mesh_resource.has_value()) {
//                     co_return tryengine::core::LogError("mesh resource: mesh_id {} not found", node_data.mesh_id);
//                 }
//                 loaded_meshes.emplace(node_data.mesh_id, std::move(*mesh_resource));
//             }
//
//             if (!loaded_materials.contains(node_data.material_id)) {
//                 auto material_resource = co_await resource_manager.GetAsync<tryengine::graphics::Material>(node_data.material_id);
//                 if (!material_resource.has_value()) {
//                     co_return tryengine::core::LogError("material resource: material_id {} not found", node_data.material_id);
//                 }
//                 loaded_materials.emplace(node_data.material_id, std::move(*material_resource));
//             }
//         }
//     }
//
//     co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());
//
//
//     tryengine::async::GetCurrentContextInfo();
//
//     // Определяем корневые ноды (ноды, которые не являются ничьими дочерними)
//     std::vector<bool> is_child(asset_map->nodes.size(), false);
//     for (const auto& node_data : asset_map->nodes) {
//         for (int32_t child_idx : node_data.children_indices) {
//             if (child_idx >= 0 && child_idx < static_cast<int32_t>(is_child.size())) {
//                 is_child[child_idx] = true;
//             }
//         }
//     }
//
//     std::vector<entt::entity> entities(asset_map->nodes.size());
//     for (size_t i = 0; i < asset_map->nodes.size(); ++i) {
//         entities[i] = reg.create();
//     }
//
//     for (size_t i = 0; i < asset_map->nodes.size(); ++i) {
//         const auto& node_data = asset_map->nodes[i];
//         entt::entity entity = entities[i];
//
//         reg.emplace<tryengine::Tag>(entity, node_data.name.empty() ? "New Node" : node_data.name);
//
//         auto transform = node_data.local_transform;
//
//         // Если это корневой объект — добавляем ему случайный сдвиг
//         if (!is_child[i]) {
//             constexpr float kSpawnSpread = 5.5f; // Настраиваемый разброс
//             transform.position.x += tryengine::core::random::Random(-kSpawnSpread, kSpawnSpread);
//             transform.position.z += tryengine::core::random::Random(-kSpawnSpread, kSpawnSpread);
//         }
//
//         reg.emplace<tryengine::Transform>(entity, transform);
//
//         auto& rel = reg.get_or_emplace<tryengine::Relationship>(entity);
//         entt::entity last_child = entt::null;
//
//         for (int32_t child_idx : node_data.children_indices) {
//             if (child_idx >= 0 && child_idx < static_cast<int32_t>(entities.size())) {
//                 entt::entity child_entity = entities[child_idx];
//                 auto& childRel = reg.get_or_emplace<tryengine::Relationship>(child_entity);
//
//                 childRel.parent = entity;
//                 rel.children++;
//
//                 if (last_child == entt::null) {
//                     rel.first = child_entity;
//                 } else {
//                     reg.get<tryengine::Relationship>(last_child).next = child_entity;
//                     childRel.prev = last_child;
//                 }
//                 last_child = child_entity;
//             }
//         }
//
//         if (node_data.mesh_id != 0 && node_data.material_id != 0) {
//             const auto& mesh_handle = loaded_meshes.at(node_data.mesh_id);
//             const auto& material_handle = loaded_materials.at(node_data.material_id);
//
//             reg.emplace<tryengine::MeshFilter>(entity, mesh_handle, node_data.mesh_id);
//             reg.emplace<tryengine::MeshRenderer>(entity, material_handle, node_data.material_id);
//         }
//     }
//
//     co_return {};
// }
//
// tryengine::async::Task<void> SpawnFast(tryengine::resources::ResourceManager& resource_manager, entt::registry& reg,
//                                         const uint64_t asset_id) {
//
//     tryengine::async::GetCurrentContextInfo();
//
//     // Структуру ассета всё равно нужно знать синхронно (сколько нод, кто чей
//     // ребёнок), поэтому её по-прежнему ждём.
//     auto asset = co_await resource_manager.GetAsync<ModelAssetMap>(asset_id);
//     if (!asset.has_value())
//         co_return tryengine::core::LogError("AssetRegistry::SpawnFast: asset_id {} not found", asset_id);
//
//     auto asset_map = *asset;
//
//     co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());
//
//     tryengine::async::GetCurrentContextInfo();
//
//     // Корневые ноды — как в оригинале.
//     std::vector<bool> is_child(asset_map->nodes.size(), false);
//     for (const auto& node_data : asset_map->nodes) {
//         for (int32_t child_idx : node_data.children_indices) {
//             if (child_idx >= 0 && child_idx < static_cast<int32_t>(is_child.size())) {
//                 is_child[child_idx] = true;
//             }
//         }
//     }
//
//     std::vector<entt::entity> entities(asset_map->nodes.size());
//     for (size_t i = 0; i < asset_map->nodes.size(); ++i) {
//         entities[i] = reg.create();
//     }
//
//     for (size_t i = 0; i < asset_map->nodes.size(); ++i) {
//         const auto& node_data = asset_map->nodes[i];
//         entt::entity entity = entities[i];
//
//         reg.emplace<tryengine::Tag>(entity, node_data.name.empty() ? "New Node" : node_data.name);
//
//         auto transform = node_data.local_transform;
//
//         if (!is_child[i]) {
//             constexpr float kSpawnSpread = 5.5f;
//             transform.position.x += tryengine::core::random::Random(-kSpawnSpread, kSpawnSpread);
//             transform.position.z += tryengine::core::random::Random(-kSpawnSpread, kSpawnSpread);
//         }
//
//         reg.emplace<tryengine::Transform>(entity, transform);
//
//         auto& rel = reg.get_or_emplace<tryengine::Relationship>(entity);
//         entt::entity last_child = entt::null;
//
//         for (int32_t child_idx : node_data.children_indices) {
//             if (child_idx >= 0 && child_idx < static_cast<int32_t>(entities.size())) {
//                 entt::entity child_entity = entities[child_idx];
//                 auto& childRel = reg.get_or_emplace<tryengine::Relationship>(child_entity);
//
//                 childRel.parent = entity;
//                 rel.children++;
//
//                 if (last_child == entt::null) {
//                     rel.first = child_entity;
//                 } else {
//                     reg.get<tryengine::Relationship>(last_child).next = child_entity;
//                     childRel.prev = last_child;
//                 }
//                 last_child = child_entity;
//             }
//         }
//
//         if (node_data.mesh_id != 0 && node_data.material_id != 0) {
//             auto mesh_handle = resource_manager.Get<tryengine::graphics::Mesh>(node_data.mesh_id);
//             auto material_handle = resource_manager.Get<tryengine::graphics::Material>(node_data.material_id);
//
//             reg.emplace<tryengine::MeshFilter>(entity, std::move(mesh_handle), node_data.mesh_id);
//             reg.emplace<tryengine::MeshRenderer>(entity, std::move(material_handle), node_data.material_id);
//         }
//     }
//
//     co_return {};
// }
//
// }  // namespace tryeditor