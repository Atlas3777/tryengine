#pragma once
#include <entt/entt.hpp>

#include "engine/core/InputState.hpp"

namespace editor {
void UpdateEditorCameraSystem(entt::registry& reg, double deltaTime, const engine::core::InputState& input);
} //namespace editor