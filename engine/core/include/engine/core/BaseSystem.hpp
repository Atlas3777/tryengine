#pragma once
#include <entt/entity/registry.hpp>

namespace tryengine::core {
void UpdateTransformSystem(entt::registry& reg);
void UpdateCameraMatrices(entt::registry& reg);
}