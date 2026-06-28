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

}  // namespace tryengine
