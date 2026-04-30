#pragma once
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace game {

void Rotate(tryengine::core::Engine& engine) {
    auto& reg = engine.GetSceneManager().GetActiveScene()->GetRegistry();
    auto view = reg.view<tryengine::Transform, tryengine::MeshRenderer>();

    float angle = 0.01f;

    for (auto entity : view) {
        auto& transform = view.get<tryengine::Transform>(entity);

        transform.rotation = glm::rotate(transform.rotation, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
}
}  // namespace game