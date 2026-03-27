#pragma once

#include <entt/core/type_info.hpp>
#include <entt/meta/factory.hpp>

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

    entt::meta_factory<engine::Hierarchy>()
        .type(entt::type_hash<engine::Hierarchy>::value(), "HierarchyComponent")
        .data<&engine::Hierarchy::parent>("Parent")
        .data<&engine::Hierarchy::depth>("Depth");
};
}  // namespace editor