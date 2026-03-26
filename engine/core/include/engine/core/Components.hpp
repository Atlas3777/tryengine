#pragma once

#include <entt/fwd.hpp>
#include <entt/resource/resource.hpp>

#include "engine/core/CoreTypes.hpp"

namespace engine {
namespace graphics {
struct Texture;
struct Mesh;
}
struct MeshFilter {
    entt::resource<graphics::Mesh> mesh;
    entt::id_type asset_id = entt::null;
};

// Сами данные материала (обычно это ресурс, как и Mesh)
struct MeshMaterial {
    // entt::resource<graphics::Texture> diffuseMap;
    entt::resource<graphics::Texture> normal_map;
    entt::id_type asset_id = entt::null;

    vec4 base_color{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
};

struct Hierarchy {
    entt::entity parent = entt::null;
    int depth = 0;  // 0 для корней, 1 для детей и т.д.
};

struct Transform {
    vec3 position{0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f};

    mat4 world_matrix{1.0f};

    mat4 GetLocalMatrix() const {
        mat4 model = mat4(1.0f);
        model = translate(model, position);
        model *= mat4_cast(rotation);
        model = glm::scale(model, scale);
        return model;
    }
};

struct AABB {
    vec3 world_min;
    vec3 world_max;
};

// Добавим простой компонент имени, если у тебя его еще нет
struct Tag {
    std::string tag;
    Tag() = default;
    Tag(const Tag&) = default;
    Tag(const std::string& t) : tag(t) {}
};
struct MainCameraTag{};
struct Camera {
    float fov = 70.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float sensitivity = 0.08f;
    float speed = 3.0f;

    // Вычисляемые векторы (обновляются системой)
    vec3 front = {0.0f, 0.0f, -1.0f};
    vec3 up = {0.0f, 1.0f, 0.0f};
    vec3 right = {1.0f, 0.0f, 0.0f};

    mat4 view_matrix = mat4(1.0f);
};
}  // namespace engine::components
