#include <entt/entt.hpp>

#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace tryengine::core {
void UpdateTransformSystem(entt::registry& reg) {
    // Лямбда для рекурсивного обхода дерева
    auto update_node = [&](auto& self, entt::entity entity, const glm::mat4& parentMatrix) -> void {
        auto& transform = reg.get<Transform>(entity);
        transform.world_matrix = parentMatrix * transform.GetLocalMatrix();

        if (auto* rel = reg.try_get<Relationship>(entity)) {
            entt::entity curr = rel->first;
            while (curr != entt::null) {
                // Передаем текущую мировую матрицу как родительскую для детей
                self(self, curr, transform.world_matrix);
                curr = reg.get<Relationship>(curr).next;
            }
        }
    };

    // 1. Находим все сущности, у которых есть Transform и Relationship (корни дерева)
    auto view = reg.view<Transform, Relationship>();
    for (auto entity : view) {
        if (view.get<Relationship>(entity).parent == entt::null) {
            update_node(update_node, entity, glm::mat4(1.0f));
        }
    }

    // 2. Одиночные сущности, у которых есть Transform, но нет Relationship
    auto noRelView = reg.view<Transform>(entt::exclude<Relationship>);
    for (auto entity : noRelView) {
        auto& transform = noRelView.get<Transform>(entity);
        transform.world_matrix = transform.GetLocalMatrix();
    }
}

void UpdateCameraMatrices(entt::registry& reg) {
    auto view = reg.view<Transform, Camera>();
    for (const auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& cam = view.get<Camera>(entity);

        cam.view_matrix = glm::inverse(transform.world_matrix);
    }
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
}  // namespace tryengine
