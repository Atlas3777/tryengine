#pragma once

#include <entt/entt.hpp>
#include <string>

namespace engine::core {

class Scene {
public:
  explicit Scene(std::string  name = "Untitled Scene");
  ~Scene() = default;

  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  Scene(Scene&&) = default;
  Scene& operator=(Scene&&) = default;

  [[nodiscard]] entt::registry& GetRegistry() const { return *registry_; }
  [[nodiscard]] const std::string& GetName() const { return name_; }

private:
  std::string name_;
  std::unique_ptr<entt::registry> registry_;
};

} // namespace engine