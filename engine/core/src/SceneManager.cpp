#include "../include/engine/core/SceneManager.hpp"

#include <iostream>
// Здесь вам понадобятся архивы (например, cereal), о которых писалось в документации EnTT
// #include <cereal/archives/binary.hpp>
// #include <fstream>

namespace engine {

Scene* SceneManager::CreateScene(const std::string& name) {
  // std::make_unique безопасно создаст новую сцену и удалит старую (если была)
  m_ActiveScene = std::make_unique<Scene>(name);
  return m_ActiveScene.get();
}

bool SceneManager::LoadScene(const std::string& filepath) {
  // 1. Создаем чистую сцену, так как snapshot_loader требует пустого registry
  auto newScene = std::make_unique<Scene>(filepath);

  // 2. Здесь должна быть логика загрузки через entt::snapshot_loader
  // Пример (псевдокод, зависит от выбранной библиотеки сериализации, например Cereal):
  /*
  std::ifstream is(filepath, std::ios::binary);
  if (!is.is_open()) return false;

  cereal::BinaryInputArchive input(is);
  entt::snapshot_loader{newScene->GetRegistry()}
      .get<entt::entity>(input)
      .get<TransformComponent>(input) // Список ваших компонентов
      .get<MeshComponent>(input)
      .orphans();
  */

  std::cout << "Loading scene from: " << filepath << "\n";

  // Перемещаем владение новой сценой в менеджер
  m_ActiveScene = std::move(newScene);
  return true;
}

bool SceneManager::SaveScene(const std::string& filepath) {
  if (!m_ActiveScene) return false;

  // Пример сериализации через entt::snapshot
  /*
  std::ofstream os(filepath, std::ios::binary);
  cereal::BinaryOutputArchive output(os);

  entt::snapshot{m_ActiveScene->GetRegistry()}
      .get<entt::entity>(output)
      .get<TransformComponent>(output)
      .get<MeshComponent>(output);
  */

  std::cout << "Saving scene to: " << filepath << "\n";
  return true;
}

} // namespace engine