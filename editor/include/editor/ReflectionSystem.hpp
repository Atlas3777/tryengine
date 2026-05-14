#pragma once

#include <entt/core/type_info.hpp>
#include <entt/meta/factory.hpp>

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/resources/AssetTypes.hpp"

namespace tryeditor {

class ReflectionSystem {
public:
    void RegisterBase() {
        entt::meta_factory<tryengine::Tag>()
            .type(entt::type_hash<tryengine::Tag>::value(), "TagComponent")
            .data<&tryengine::Tag::tag>("Tag");

        entt::meta_factory<tryengine::Transform>()
            .type(entt::type_hash<tryengine::Transform>::value(), "TransformComponent")
            .data<&tryengine::Transform::position>("Position")
            .data<&tryengine::Transform::rotation>("Rotation")
            .data<&tryengine::Transform::scale>("Scale");

        entt::meta_factory<tryengine::Relationship>()
            .type(entt::type_hash<tryengine::Relationship>::value(), "Relationship")
            .data<&tryengine::Relationship::children>("Children Count")
            .data<&tryengine::Relationship::first>("First Child")
            .data<&tryengine::Relationship::prev>("Prev Sibling")
            .data<&tryengine::Relationship::next>("Next Sibling")
            .data<&tryengine::Relationship::parent>("Parent");

        entt::meta_factory<tryengine::Camera>()
            .type(entt::type_hash<tryengine::Camera>::value(), "CameraComponent")
            .data<&tryengine::Camera::fov>("FOV")
            .data<&tryengine::Camera::near_plane>("Near Plane")
            .data<&tryengine::Camera::far_plane>("Far Plane")
            .data<&tryengine::Camera::sensitivity>("Sensitivity")
            .data<&tryengine::Camera::speed>("Speed");

        entt::meta_factory<EditorCameraTag>().type(entt::type_hash<EditorCameraTag>::value(), "EditorCameraTag");

        entt::meta_factory<tryengine::MainCameraTag>().type(entt::type_hash<tryengine::MainCameraTag>::value(),
                                                            "MainCameraTag");

        entt::meta_factory<tryengine::MeshFilter>()
            .type(entt::type_hash<tryengine::MeshFilter>::value(), "MeshFilter")
            .data<&tryengine::MeshFilter::asset_id>("asset_id")
            .data<&tryengine::MeshFilter::mesh>("mesh_ptr");

        entt::meta_factory<tryengine::MeshRenderer>()
            .type(entt::type_hash<tryengine::MeshRenderer>::value(), "MeshRenderer")
            .data<&tryengine::MeshRenderer::material>("material_instance")
            .data<&tryengine::MeshRenderer::asset_id>("material_asset_id")
            .custom<tryengine::AssetTypeID>(tryengine::AssetType::Material);
    }
};
}  // namespace tryeditor