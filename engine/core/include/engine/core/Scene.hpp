#pragma once

#include <entt/entt.hpp>
#include <string>

namespace engine::core {

class Scene {
public:
  explicit Scene(std::string  name = "Untitled Scene");
  ~Scene() = default;

  // СТРОГИЙ ЗАПРЕТ КОПИРОВАНИЯ
  // Мы не хотим случайно скопировать весь registry и получить рассинхронизацию.
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  // РАЗРЕШАЕМ ПЕРЕМЕЩЕНИЕ
  // Полезно, если мы хотим возвращать сцену из фабрики или хранить в контейнерах.
  Scene(Scene&&) = default;
  Scene& operator=(Scene&&) = default;

  // Доступ к registry
  entt::registry& GetRegistry() { return m_Registry; }
  const entt::registry& GetRegistry() const { return m_Registry; }

  const std::string& GetName() const { return m_Name; }

  // Создание сущности-хелпера (удобная обертка)
  entt::entity CreateEntity();
  void DestroyEntity(entt::entity entity);

private:
  std::string m_Name;
  entt::registry m_Registry;
};

} // namespace engine