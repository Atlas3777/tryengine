// #pragma once
//
// #include "ComponentRegistry.hpp"
// #include "Components.hpp"
// #include "ResourceManager.hpp"
//
// namespace tryengine {
// template<class Archive>
// void serialize(Archive& ar,tryengine::Camera& camera){
//     ar(cereal::make_nvp("fov", camera.fov));
//     ar(cereal::make_nvp("near_plane", camera.near_plane));
//     ar(cereal::make_nvp("far_plane", camera.far_plane));
//     ar(cereal::make_nvp("sensitivity", camera.sensitivity));
//     ar(cereal::make_nvp("speed", camera.speed));
// };
// }
//
// template <class Archive>
// void save(Archive& ar, const tryengine::MeshRenderer& obj) {
//     ar(cereal::make_nvp("asset_id", obj.asset_id));
// }
//
// namespace cereal {
// template <class Archive>
// void load(Archive& ar, tryengine::core::WithManager<tryengine::MeshRenderer>& wrapper) {
//     auto& obj = wrapper.object;
//
//     uint64_t id;
//     ar(make_nvp("asset_id", id));
//
//     obj.asset_id = id;
//     obj.material = wrapper.manager.Get<tryengine::graphics::Material>(id);
// }
// }