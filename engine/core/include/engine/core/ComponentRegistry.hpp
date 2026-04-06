#pragma once

#include <cereal/archives/binary.hpp>
#include <entt/entt.hpp>

#include "ResourceManager.hpp"
namespace engine::core {
class ComponentRegistry {
public:
    template <typename T>
    void Register() {
        // serializers_.push_back(&SerializeImpl<T>);
        // deserializers_.push_back(&DeserializeImpl<T>);
        // post_load_.push_back(&PostLoadImpl<T>);
    }

    void Serialize(const entt::registry& reg, cereal::BinaryOutputArchive& ar) {
        entt::snapshot snap{reg};
        snap.get<entt::entity>(ar);

        for (auto fn : serializers_) {
            fn(snap, ar);
        }
    }

    void Deserialize(entt::registry& reg, cereal::BinaryInputArchive& ar) {
        entt::snapshot_loader loader{reg};
        loader.get<entt::entity>(ar);

        for (auto fn : deserializers_) {
            fn(loader, ar);
        }

        loader.orphans();
    }

    void PostLoad(entt::registry& reg, ResourceManager& rm) {
        for (auto fn : post_load_) {
            fn(reg, rm);
        }
    }

private:
    using SaveFn = void (*)(entt::snapshot&, cereal::BinaryOutputArchive&);
    using LoadFn = void (*)(entt::snapshot_loader&, cereal::BinaryInputArchive&);
    using PostFn = void (*)(entt::registry&, ResourceManager&);

    std::vector<SaveFn> serializers_;
    std::vector<LoadFn> deserializers_;
    std::vector<PostFn> post_load_;

    // template<typename T>
    // static void SerializeImpl(entt::snapshot& snap, cereal::BinaryOutputArchive& ar) {
    //     SerializationTraits<T>::Save(snap, ar);
    // }
    //
    // template<typename T>
    // static void DeserializeImpl(entt::snapshot_loader& loader, cereal::BinaryInputArchive& ar) {
    //     SerializationTraits<T>::Load(loader, ar);
    // }
    //
    // template<typename T>
    // static void PostLoadImpl(entt::registry& reg, engine::resources::ResourceManager& rm) {
    //     if constexpr (requires { SerializationTraits<T>::PostLoad(reg, rm); }) {
    //         SerializationTraits<T>::PostLoad(reg, rm);
    //     }
    // }
};
}  // namespace engine::core