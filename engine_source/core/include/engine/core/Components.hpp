#pragma once

#include <entt/entity/entity.hpp>
#include <entt/resource/resource.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

namespace tryengine {

struct LightComponent {
    glm::vec3 color{1.0f, 1.0f, 1.0f};  // Цвет света (RGB)
    float intensity{1.0f};              // Интенсивность (яркость)
    float radius{10.0f};                // Радиус освещения (Attenuation)
};

struct Relationship {
    std::size_t children{};
    entt::entity first{entt::null};
    entt::entity prev{entt::null};
    entt::entity next{entt::null};
    entt::entity parent{entt::null};
};

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 world_matrix{1.0f};

    glm::mat4 GetLocalMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = translate(model, position);
        model *= mat4_cast(rotation);
        model = glm::scale(model, scale);
        return model;
    }
};

struct AABB {
    glm::vec3 world_min;
    glm::vec3 world_max;
};

struct Tag {
    Tag() = default;
    Tag(std::string t) : tag(std::move(t)) {}
    std::string tag;
};

struct MainCameraTag {};
struct Camera {
    glm::mat4 view_matrix{1.0f};

    // Параметры проекции
    float fov = 45.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;

    // Параметры управления (для системы ввода)
    float sensitivity = 0.06f;
    float speed = 5.0f;
};
}  // namespace tryengine
