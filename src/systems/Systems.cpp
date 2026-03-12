#include "Systems.hpp"

#include <entt/entt.hpp>

#include "EngineTypes.hpp"

void UpdateEditorCameraSystem(entt::registry& reg, double deltaTime, const InputState& input) {
    if (!input.IsMouseButtonDown(MouseButton::Right)) return;
    auto view = reg.view<TransformComponent, CameraComponent, EditorCameraTag>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& cam = view.get<CameraComponent>(entity);

        if (input.isCursorCaptured) {
            transform.rotation.y += input.mouseDeltaX * cam.sensitivity;  // Yaw
            transform.rotation.x -= input.mouseDeltaY * cam.sensitivity;  // Pitch
            transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);
        }

        glm::vec3 front;
        front.x = std::cos(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));
        front.y = std::sin(glm::radians(transform.rotation.x));
        front.z = std::sin(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));

        cam.front = glm::normalize(front);
        cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0, 1, 0)));
        cam.up = glm::normalize(glm::cross(cam.right, cam.front));

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
// TODO: Если у сущности есть HierarchyComponent, но нет TransformComponent, или если parent был удален, но ссылка
// осталась — приложение упадет.
void UpdateTransformSystem(entt::registry& reg) {
    // 1. Сортируем сущности по глубине иерархии
    // Это гарантирует, что родитель всегда обработается раньше ребенка
    reg.sort<HierarchyComponent>([](const auto& lhs, const auto& rhs) { return lhs.depth < rhs.depth; });

    auto view = reg.view<TransformComponent, HierarchyComponent>();

    view.each([&](entt::entity entity, TransformComponent& transform, HierarchyComponent& hierarchy) {
        // Вычисляем локальную матрицу из pos/rot/scale
        glm::mat4 localMatrix = transform.GetLocalMatrix();

        if (hierarchy.parent == entt::null) {
            // Если корнь — мировая матрица равна локальной
            transform.worldMatrix = localMatrix;
        } else {
            // Если есть родитель — умножаем его мировую на нашу локальную
            // Важно: родитель уже обновлен благодаря сортировке!
            auto& parentTransform = reg.get<TransformComponent>(hierarchy.parent);
            transform.worldMatrix = parentTransform.worldMatrix * localMatrix;
        }
    });
}
void UpdateAABBSystem(entt::registry& reg) {
    auto view = reg.view<TransformComponent, MeshComponent, AABBComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& mesh = view.get<MeshComponent>(entity);
        auto& aabb = view.get<AABBComponent>(entity);

        // Масштабируем и вращаем локальные границы
        glm::mat4 model = transform.worldMatrix;
        glm::vec3 min = mesh.mesh->localMin;
        glm::vec3 max = mesh.mesh->localMax;

        // Метод Джима Арво (быстрая трансформация AABB)
        glm::vec3 worldMin = glm::vec3(model[3]);
        glm::vec3 worldMax = worldMin;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                float a = model[j][i] * min[j];
                float b = model[j][i] * max[j];
                worldMin[i] += glm::min(a, b);
                worldMax[i] += glm::max(a, b);
            }
        }
        aabb.worldMin = worldMin;
        aabb.worldMax = worldMax;
    }
}
