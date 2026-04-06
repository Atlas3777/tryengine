#pragma once

#include <entt/core/type_info.hpp>
#include <entt/meta/factory.hpp>

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"

namespace editor {
void RegisterRef() {
    entt::meta_factory<engine::Tag>()
        .type(entt::type_hash<engine::Tag>::value(), "TagComponent")
        .data<&engine::Tag::tag>("Tag");  // Для полей достаточно просто строки

    entt::meta_factory<engine::Transform>()
        .type(entt::type_hash<engine::Transform>::value(), "TransformComponent")
        .data<&engine::Transform::position>("Position")
        .data<&engine::Transform::rotation>("Rotation")
        .data<&engine::Transform::scale>("Scale");

    entt::meta_factory<engine::Relationship>()
        .type(entt::type_hash<engine::Relationship>::value(), "Relationship")
        .data<&engine::Relationship::children>("Children Count")
        .data<&engine::Relationship::first>("First Child")
        .data<&engine::Relationship::prev>("Prev Sibling")
        .data<&engine::Relationship::next>("Next Sibling")
        .data<&engine::Relationship::parent>("Parent");

    entt::meta_factory<engine::Camera>()
        .type(entt::type_hash<engine::Camera>::value(), "CameraComponent")
        .data<&engine::Camera::fov>("FOV")
        .data<&engine::Camera::near_plane>("Near Plane")
        .data<&engine::Camera::far_plane>("Far Plane")
        .data<&engine::Camera::sensitivity>("Sensitivity")
        .data<&engine::Camera::speed>("Speed")
        ;

    entt::meta_factory<EditorCameraTag>().type(entt::type_hash<EditorCameraTag>::value(), "EditorCameraTag");

    entt::meta_factory<engine::MainCameraTag>().type(entt::type_hash<engine::MainCameraTag>::value(), "MainCameraTag");

    entt::meta_factory<engine::MeshFilter>()
        .type(entt::type_hash<engine::MeshFilter>::value(), "MeshFilter")
        .data<&engine::MeshFilter::asset_id>("asset_id")
        .data<&engine::MeshFilter::mesh>("mesh_ptr");

    entt::meta_factory<engine::graphics::MeshRenderer>()
        .type(entt::type_hash<engine::graphics::MeshRenderer>::value(), "MeshRenderer")
        .data<&engine::graphics::MeshRenderer::material_instance>("material_instance");
};
}  // namespace editor