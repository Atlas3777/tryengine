#pragma once
#include <entt/entt.hpp>

#include "engine/core/InputState.hpp"

namespace tryeditor {
void UpdateEditorCameraSystem(entt::registry& reg, double delta_time, const tryengine::core::InputState& input);
} //namespace tryeditor