#pragma once

#include <memory>
#include <string>

#include "Scene.hpp"

namespace engine::core {

class SceneManager {
public:
  SceneManager() = default;
  ~SceneManager() = default;

  // Запрещаем копирование менеджера
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  // Разрешаем перемещение
  SceneManager(SceneManager&&) = default;
  SceneManager& operator=(SceneManager&&) = default;

  // Создает новую пустую сцену и делает её активной
  Scene* CreateScene(const std::string& name);

  // Загружает сцену из файла (используя EnTT snapshot loader под капотом)
  bool LoadScene(const std::string& filepath);

  // Сохраняет текущую сцену (используя EnTT snapshot)
  bool SaveScene(const std::string& filepath);

  // Получение текущей активной сцены
  Scene* GetActiveScene() { return m_ActiveScene.get(); }

private:
  // Менеджер эксклюзивно владеет активной сценой
  std::unique_ptr<Scene> m_ActiveScene = nullptr;
};

} // namespace engine