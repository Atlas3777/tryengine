#include "Systems.hpp"

#include <entt/entt.hpp>

#include "EngineTypes.hpp"

void UpdateEditorCameraSystem(entt::registry& reg, double deltaTime, const InputState& input) {
    auto view = reg.view<TransformComponent, CameraComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& cam = view.get<CameraComponent>(entity);

        // 1. Поворот через мышь (используем mouseDelta из твоего InputState)
        if (input.isCursorCaptured) {
            transform.rotation.y += input.mouseDeltaX * cam.sensitivity;  // Yaw
            transform.rotation.x -= input.mouseDeltaY * cam.sensitivity;  // Pitch
            transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);
        }

        // 2. Обновление векторов направления
        glm::vec3 front;
        front.x = std::cos(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));
        front.y = std::sin(glm::radians(transform.rotation.x));
        front.z = std::sin(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));

        cam.front = glm::normalize(front);
        cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0, 1, 0)));
        cam.up = glm::normalize(glm::cross(cam.right, cam.front));

        // 3. Движение (используем IsKeyDown из твоего InputState)
        float moveSpeed = cam.speed * static_cast<float>(deltaTime);

        if (input.IsKeyDown(SDL_SCANCODE_W)) transform.position += cam.front * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_S)) transform.position -= cam.front * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_A)) transform.position -= cam.right * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_D)) transform.position += cam.right * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_E)) transform.position += cam.up * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_Q)) transform.position -= cam.up * moveSpeed;

        cam.viewMatrix = glm::lookAt(transform.position, transform.position + cam.front, cam.up);
    }
}
void UpdateHierarchySystem(entt::registry& reg) {
    // 1. Сортируем иерархию так, чтобы родители всегда были перед детьми
    // Это гарантирует, что worldMatrix родителя уже вычислена к моменту обработки ребенка
    reg.sort<HierarchyComponent>([&reg](const entt::entity lhs, const entt::entity rhs) {
        const auto& h_lhs = reg.get<HierarchyComponent>(lhs);
        const auto& h_rhs = reg.get<HierarchyComponent>(rhs);

        // Если левый объект — родитель правого, он должен быть выше (меньше)
        return h_rhs.parent == lhs;
    });

    // 2. Проходим по всем объектам с трансформацией
    auto view = reg.view<TransformComponent>();

    // Используем обычный цикл, так как порядок важен (после sort)
    view.each([&](entt::entity entity, TransformComponent& transform) {
        auto* hierarchy = reg.try_get<HierarchyComponent>(entity);

        if (hierarchy && hierarchy->parent != entt::null) {
            // Если есть родитель, умножаем его мировую матрицу на нашу локальную
            const auto& parentTransform = reg.get<TransformComponent>(hierarchy->parent);
            transform.worldMatrix = parentTransform.worldMatrix * transform.GetLocalMatrix();
        } else {
            // Если родителя нет, мировая матрица равна локальной
            transform.worldMatrix = transform.GetLocalMatrix();
        }
    });
}
