#pragma once

#include <entt/entity/entity.hpp>
#include <entt/resource/resource.hpp>

#include "engine/core/CoreTypes.hpp"
#include "engine/core/GLMSerialization.hpp"

namespace tryengine {
namespace graphics {
struct Material;
struct Mesh;
}  // namespace graphics

struct MeshFilter {
    entt::resource<graphics::Mesh> mesh;
    uint64_t asset_id = 0;
};
struct MeshRenderer {
    entt::resource<graphics::Material> material;
    uint64_t asset_id = 0;
};

struct Relationship {
    std::size_t children{};
    entt::entity first{entt::null};
    entt::entity prev{entt::null};
    entt::entity next{entt::null};
    entt::entity parent{entt::null};
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

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("position", position), cereal::make_nvp("rotation", rotation),
                cereal::make_nvp("scale", scale));
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
