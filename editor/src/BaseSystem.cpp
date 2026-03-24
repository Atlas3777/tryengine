#include <entt/entt.hpp>

#include "../../engine/core/include/engine/core/CoreTypes.hpp"
#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace editor {
using namespace engine::core;
using namespace engine;
void UpdateEditorCameraSystem(entt::registry& reg, double deltaTime, const InputState& input) {
    if (!input.mouseButtons[3]) return;
    auto view = reg.view<engine::Transform, engine::Camera, EditorCameraTag>();

    for (auto entity : view) {
        auto& transform = view.get<engine::Transform>(entity);
        auto& cam = view.get<Camera>(entity);

        // if (input.isCursorCaptured) {
            transform.rotation.y += input.mouseDeltaX * cam.sensitivity;  // Yaw
            transform.rotation.x -= input.mouseDeltaY * cam.sensitivity;  // Pitch
            transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);
        // }

        vec3 front;
        front.x = std::cos(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));
        front.y = std::sin(glm::radians(transform.rotation.x));
        front.z = std::sin(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));

        cam.front = glm::normalize(front);
        cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0, 1, 0)));
        cam.up = glm::normalize(glm::cross(cam.right, cam.front));

        float moveSpeed = cam.speed * static_cast<float>(deltaTime);

        if (input.IsDown(Key::W)) transform.position += cam.front * moveSpeed;
        if (input.IsDown(Key::S)) transform.position -= cam.front * moveSpeed;
        if (input.IsDown(Key::A)) transform.position -= cam.right * moveSpeed;
        if (input.IsDown(Key::D)) transform.position += cam.right * moveSpeed;
        if (input.IsDown(Key::E)) transform.position += cam.up * moveSpeed;
        if (input.IsDown(Key::Q)) transform.position -= cam.up * moveSpeed;

        cam.viewMatrix = glm::lookAt(transform.position, transform.position + cam.front, cam.up);
    }
}
}  // namespace editor
