#include <entt/entt.hpp>

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace editor {

void UpdateEditorCameraSystem(entt::registry& reg, const double delta_time, const engine::core::InputState& input) {
    auto view = reg.view<engine::Transform, engine::Camera, EditorCameraTag>();

    for (auto entity : view) {
        auto& transform = view.get<engine::Transform>(entity);
        auto& cam = view.get<engine::Camera>(entity);

        // 1. Вращение
        if (input.IsMouseDown(engine::core::Mouse::Right)) {
            // Создаем кватернионы вращения вокруг осей мира (или локальных)
            // Важно: для Yaw используем глобальную ось Y (0,1,0), чтобы не было "завала" горизонта
            float yawSign = 1.0f;
            glm::quat qYaw = glm::angleAxis(glm::radians(-input.mouseDeltaX * cam.sensitivity), glm::vec3(0, 1, 0));

            // Для Pitch используем локальную ось Right
            glm::vec3 right = transform.rotation * glm::vec3(1, 0, 0);
            glm::quat qPitch = glm::angleAxis(glm::radians(-input.mouseDeltaY * cam.sensitivity), right);

            transform.rotation = qYaw * qPitch * transform.rotation;
            transform.rotation = glm::normalize(transform.rotation);
        }

        // 2. Получаем векторы направления из кватерниона
        // В GLM: по умолчанию Forward — это (0, 0, -1)
        glm::vec3 front = transform.rotation * glm::vec3(0, 0, -1);
        glm::vec3 right = transform.rotation * glm::vec3(1, 0, 0);
        glm::vec3 up = transform.rotation * glm::vec3(0, 1, 0);

        // 3. Движение
        if (input.IsMouseDown(engine::core::Mouse::Right)) {
            float moveSpeed = cam.speed * static_cast<float>(delta_time);
            if (input.IsDown(engine::core::Key::W))
                transform.position += front * moveSpeed;
            if (input.IsDown(engine::core::Key::S))
                transform.position -= front * moveSpeed;
            if (input.IsDown(engine::core::Key::A))
                transform.position -= right * moveSpeed;
            if (input.IsDown(engine::core::Key::D))
                transform.position += right * moveSpeed;
            if (input.IsDown(engine::core::Key::E))
                transform.position += up * moveSpeed;
            if (input.IsDown(engine::core::Key::Q))
                transform.position -= up * moveSpeed;
        }
    }
}
}  // namespace editor
