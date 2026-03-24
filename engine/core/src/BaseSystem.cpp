#include <entt/entt.hpp>

#include "../include/engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace engine {
using namespace engine::core;
// TODO: Если у сущности есть Hierarchy, но нет Transform, или если parent был удален, но ссылка
// осталась — приложение упадет.
void UpdateTransformSystem(entt::registry& reg) {
    // 1. Сортируем сущности по глубине иерархии
    // Это гарантирует, что родитель всегда обработается раньше ребенка
    reg.sort<Hierarchy>([](const auto& lhs, const auto& rhs) { return lhs.depth < rhs.depth; });

    auto view = reg.view<Transform, Hierarchy>();

    view.each([&](entt::entity entity, Transform& transform, Hierarchy& hierarchy) {
        // Вычисляем локальную матрицу из pos/rot/scale
        glm::mat4 localMatrix = transform.GetLocalMatrix();

        if (hierarchy.parent == entt::null) {
            // Если корнь — мировая матрица равна локальной
            transform.worldMatrix = localMatrix;
        } else {
            // Если есть родитель — умножаем его мировую на нашу локальную
            // Важно: родитель уже обновлен благодаря сортировке!
            auto& parentTransform = reg.get<Transform>(hierarchy.parent);
            transform.worldMatrix = parentTransform.worldMatrix * localMatrix;
        }
    });
}
void UpdateAABBSystem(entt::registry& reg) {
    // auto view = reg.view<Transform, MeshRenderer, AABB>();
    // for (auto entity : view) {
    //     auto& transform = view.get<Transform>(entity);
    //     auto& mesh = view.get<MeshRenderer>(entity);
    //     auto& aabb = view.get<AABB>(entity);
    //
    //     // Масштабируем и вращаем локальные границы
    //     mat4 model = transform.worldMatrix;
    //     vec3 min = mesh.mesh->localMin;
    //     vec3 max = mesh.mesh->localMax;
    //
    //     // Метод Джима Арво (быстрая трансформация AABB)
    //     vec3 worldMin = vec3(model[3]);
    //     vec3 worldMax = worldMin;
    //     for (int i = 0; i < 3; i++) {
    //         for (int j = 0; j < 3; j++) {
    //             float a = model[j][i] * min[j];
    //             float b = model[j][i] * max[j];
    //             worldMin[i] += glm::min(a, b);
    //             worldMax[i] += glm::max(a, b);
    //         }
    //     }
    //     aabb.worldMin = worldMin;
    //     aabb.worldMax = worldMax;
    // }
}
}  // namespace engine
