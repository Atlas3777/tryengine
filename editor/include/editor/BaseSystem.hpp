#pragma once

#include "../../../engine/core/include/engine/core/Input.hpp"

namespace editor {
void UpdateEditorCameraSystem(entt::registry& reg, double deltaTime, const engine::InputState& input);
} //namespace editor