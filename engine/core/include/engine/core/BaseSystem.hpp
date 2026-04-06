#pragma once
#include <entt/entity/registry.hpp>

namespace engine::core {
void UpdateTransformSystem(entt::registry& reg);
void UpdateCameraMatrices(entt::registry& reg);
}