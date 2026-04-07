#pragma once
#include <entt/entt.hpp>

#include "engine/core/InputState.hpp"

namespace editor {
void UpdateEditorCameraSystem(entt::registry& reg, double delta_time, const engine::core::InputState& input);
} //namespace editor