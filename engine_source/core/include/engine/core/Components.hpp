#pragma once

#include <entt/entity/entity.hpp>
#include <entt/resource/resource.hpp>
#include <utility>

#include "engine/core/CoreTypes.hpp"
#include "engine/core/GLMSerialization.hpp"
#include "engine/core/ResourceManager.hpp"

namespace tryengine {

namespace graphics {
struct Material;
struct Mesh;
}  // namespace graphics

struct MeshFilter {
    entt::resource<graphics::Mesh> mesh;
    uint64_t asset_id = 0;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("asset_id", asset_id));
    }

    void Resolve(core::ResourceManager& rm) {
        if (!mesh && asset_id != 0) {
            mesh = rm.Get<graphics::Mesh>(asset_id);
        }
    }
};

struct MeshRenderer {
    entt::resource<graphics::Material> material;
    uint64_t asset_id = 0;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("asset_id", asset_id));
    }

    void Resolve(core::ResourceManager& rm) {
        if (!material && asset_id != 0) {
            material = rm.Get<graphics::Material>(asset_id);
        }
    }
};

struct Relationship {
    std::size_t children{};
    entt::entity first{entt::null};
    entt::entity prev{entt::null};
    entt::entity next{entt::null};
    entt::entity parent{entt::null};

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("children", children));
        ar(cereal::make_nvp("first", first));
        ar(cereal::make_nvp("prev", prev));
        ar(cereal::make_nvp("next", next));
        ar(cereal::make_nvp("parent", parent));
    }
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

struct Tag {
    Tag() = default;

    Tag(std::string t) : tag(std::move(t)) {}

    std::string tag;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("tag", tag));
    }
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

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("fov", fov));
        ar(cereal::make_nvp("near_plane", near_plane));
        ar(cereal::make_nvp("far_plane", far_plane));
        ar(cereal::make_nvp("sensitivity", sensitivity));
        ar(cereal::make_nvp("speed", speed));
    }
};
}  // namespace tryengine
