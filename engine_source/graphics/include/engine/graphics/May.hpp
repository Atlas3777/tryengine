#pragma once
#include <entt/entity/registry.hpp>

namespace tryengine::graphics {
class RenderSystem;

void SubmitSceneFromEnTT(entt::registry& reg, entt::entity camera_entity,
                         tryengine::graphics::RenderSystem& render_system);
}
