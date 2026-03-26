#include "../include/engine/core/Scene.hpp"

#include <utility>

namespace engine::core {

Scene::Scene(std::string  name)
    : name_(std::move(name)) {
    registry_ = std::make_unique<entt::registry>();
}
//
// entt::entity Scene::CreateEntity() {
//   return registry_.create();
// }
//
// void Scene::DestroyEntity(entt::entity entity) {
//   registry_.destroy(entity);
// }

} // namespace engine