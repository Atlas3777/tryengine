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
  entt::registry& GetRegistry() { return *registry_; }
  // const entt::registry& GetRegistry() const { return registry_.get(); }

  const std::string& GetName() const { return name_; }

  // Создание сущности-хелпера (удобная обертка)
  entt::entity CreateEntity();
  void DestroyEntity(entt::entity entity);

private:
  std::string name_;
  std::unique_ptr<entt::registry> registry_;
};

} // namespace engine