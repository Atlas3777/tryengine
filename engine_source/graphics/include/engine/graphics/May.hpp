#pragma once
#include <entt/entity/registry.hpp>

namespace tryengine::graphics {
class RenderSystem;

void SubmitSceneFromEnTT(entt::registry& reg,
                         tryengine::graphics::RenderSystem& render_system);
}
