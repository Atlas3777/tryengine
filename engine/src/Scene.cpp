#include <utility>

#include "engine/Scene.hpp"

namespace engine {

Scene::Scene(std::string  name)
    : m_Name(std::move(name)) {
}

entt::entity Scene::CreateEntity() {
  return m_Registry.create();
}

void Scene::DestroyEntity(entt::entity entity) {
  m_Registry.destroy(entity);
}

} // namespace engine