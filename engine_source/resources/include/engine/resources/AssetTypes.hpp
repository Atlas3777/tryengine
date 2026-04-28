#pragma once
#include <entt/core/hashed_string.hpp>

namespace tryengine {

using AssetTypeID = entt::id_type;

// namespace используется как контейнер констант, вычисляемых при компиляции
namespace AssetType {
constexpr AssetTypeID GlslShader = entt::hashed_string{"glsl_shader"}.value();
constexpr AssetTypeID Shader = entt::hashed_string{"shader"}.value();
constexpr AssetTypeID Material = entt::hashed_string{"material"}.value();
constexpr AssetTypeID Mesh = entt::hashed_string{"mesh"}.value();
constexpr AssetTypeID Texture = entt::hashed_string{"texture"}.value();
constexpr AssetTypeID Prefab = entt::hashed_string{"prefab"}.value();
constexpr AssetTypeID Gltf = entt::hashed_string{"gltf"}.value();
}  // namespace AssetType

}  // namespace tryengine