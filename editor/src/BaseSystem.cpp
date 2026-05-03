#include <entt/entt.hpp>

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputState.hpp"

namespace tryeditor {

void UpdateEditorCameraSystem(entt::registry& reg, const double delta_time, const tryengine::core::InputState& input) {
    auto view = reg.view<tryengine::Transform, tryengine::Camera, EditorCameraTag>();

    for (auto entity : view) {
        auto& transform = view.get<tryengine::Transform>(entity);
        auto& camera = view.get<tryengine::Camera>(entity);

        if (input.IsMouseDown(tryengine::core::Mouse::Right)) {
            float yawSign = 1.0f;
            glm::quat qYaw = glm::angleAxis(glm::radians(-input.mouseDeltaX * camera.sensitivity), glm::vec3(0, 1, 0));

            // Для Pitch используем локальную ось Right
            glm::vec3 right = transform.rotation * glm::vec3(1, 0, 0);
            glm::quat qPitch = glm::angleAxis(glm::radians(-input.mouseDeltaY * camera.sensitivity), right);

            transform.rotation = qYaw * qPitch * transform.rotation;
            transform.rotation = glm::normalize(transform.rotation);
        }

        glm::vec3 front = transform.rotation * glm::vec3(0, 0, -1);
        glm::vec3 right = transform.rotation * glm::vec3(1, 0, 0);
        glm::vec3 up = transform.rotation * glm::vec3(0, 1, 0);

        // 3. Движение
        if (input.IsMouseDown(tryengine::core::Mouse::Right)) {
            float moveSpeed = camera.speed * static_cast<float>(delta_time);
            if (input.IsDown(tryengine::core::Key::W))
                transform.position += front * moveSpeed;
            if (input.IsDown(tryengine::core::Key::S))
                transform.position -= front * moveSpeed;
            if (input.IsDown(tryengine::core::Key::A))
                transform.position -= right * moveSpeed;
            if (input.IsDown(tryengine::core::Key::D))
                transform.position += right * moveSpeed;
            if (input.IsDown(tryengine::core::Key::E))
                transform.position += up * moveSpeed;
            if (input.IsDown(tryengine::core::Key::Q))
                transform.position -= up * moveSpeed;
        }
    }
}
}  // namespace tryeditor
