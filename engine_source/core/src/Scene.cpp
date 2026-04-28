#include "engine/core/Scene.hpp"

#include <utility>

namespace tryengine::core {

Scene::Scene(std::string  name)
    : name_(std::move(name)) {
    registry_ = std::make_unique<entt::registry>();
}
} // namespace tryengine