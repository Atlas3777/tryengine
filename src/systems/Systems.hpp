#pragma once

#include <entt/entity/fwd.hpp>

#include "EngineState.hpp"
void UpdateEditorCameraSystem(entt::registry& reg, double dt, const InputState& input);
void UpdateHierarchySystem(entt::registry& reg);
